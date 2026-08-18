//  The MIT License
//
//  Copyright (C) 2025 Giuseppe Mastrangelo
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  'Software'), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be
//  included in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#include "version.h"
#include "httplib.h"
#include "popl.hpp"
#include "StdioTransport.h"
#include "SseTransport.h"
#include "server/Server.h"
#include "aixlog.hpp"
#include "loader/PluginRegistry.h"
#include "runtime/PluginRuntime.h"
#include "tools/ToolProfile.h"
#include "json.hpp"
#include "utils/MCPBuilder.h"
#include <algorithm>
#include <chrono>
#include <csignal>

using namespace popl;

std::shared_ptr<vx::mcp::Server> server;

struct NotificationState {
    std::mutex serverNotificationMutex;
};
NotificationState notificationState;

/// stop handler Ctrl+C
void stop_handler(sig_atomic_t s) {
    std::cout <<"Stopping server..." << std::endl;
    if (server && server->IsValid()) {
        server->Stop();
    }
    std::cout << "done." << std::endl;
    exit(0);
}

/// Notification Implementation from plugins to mcp-client
void ClientNotificationCallbackImpl(
    void*,
    const char* pluginName,
    const char* notification) {
    std::lock_guard<std::mutex> lock(notificationState.serverNotificationMutex);
    if (server && server->IsValid()) {
        server->SendNotification(pluginName, notification);
    }
}

/// main entry point
int main(int argc, char **argv) {
    std::string name;
    std::string plugins_directory;
    std::string logs_directory;
    std::string plugin_staging_directory;
    int plugin_debounce_ms;
    int plugin_delete_grace_ms;
    int plugin_retain_generations;
    std::string tool_profile_name;
    std::string tool_profiles_file;
    bool verbose;

    std::shared_ptr<vx::ITransport> transport;
    auto plugin_runtime = std::make_shared<vx::mcp::PluginRuntime>();
    server = std::make_shared<vx::mcp::Server>();

    //============================================================================================
    // setup signal handler (Ctrl+C)
    //============================================================================================
    signal(SIGINT, stop_handler);

    //============================================================================================
    // setup command line options
    //============================================================================================
    OptionParser op("Allowed options");
    auto help_option = op.add<Switch>("", "help", "produce help message");
    auto name_option = op.add<Value<std::string>>("n", "name", "the name of the server", "mcp-server");
    auto plugins_directory_option = op.add<Value<std::string>>("p", "plugins", "the directory where to load the plugins", "./plugins");
    auto logs_directory_option = op.add<Value<std::string>>("l", "logs", "the directory where to store the logs", "./logs");
    auto plugin_staging_option = op.add<Value<std::string>>("", "plugin-staging", "the directory for staged plugin generations", "./build_dev/plugin-staging");
    auto plugin_hot_reload = op.add<Switch>("", "plugin-hot-reload", "watch plugin files and reload them atomically");
    auto plugin_debounce_option = op.add<Value<int>>("", "plugin-debounce-ms", "plugin watcher debounce in milliseconds", 300);
    auto plugin_delete_grace_option = op.add<Value<int>>("", "plugin-delete-grace-ms", "plugin deletion grace period in milliseconds", 500);
    auto plugin_retain_option = op.add<Value<int>>("", "plugin-retain-generations", "successful staged generations to retain", 2);
    auto tool_profile_option = op.add<Value<std::string>>("", "tool-profile", "tool exposure profile (all or configured profile)", "all");
    auto tool_profiles_file_option = op.add<Value<std::string>>("", "tool-profiles-file", "JSON file containing tool profiles", "./config/tool-profiles.json");
    auto verbose_option = op.add<Value<bool>>("v", "verbose", "enable verbose", verbose);
    auto use_sse_server = op.add<Switch>("s", "sse", "start as sse server");
    name_option->assign_to(&name);
    plugins_directory_option->assign_to(&plugins_directory);
    logs_directory_option->assign_to(&logs_directory);
    plugin_staging_option->assign_to(&plugin_staging_directory);
    plugin_debounce_option->assign_to(&plugin_debounce_ms);
    plugin_delete_grace_option->assign_to(&plugin_delete_grace_ms);
    plugin_retain_option->assign_to(&plugin_retain_generations);
    tool_profile_option->assign_to(&tool_profile_name);
    tool_profiles_file_option->assign_to(&tool_profiles_file);
    verbose_option->assign_to(&verbose);

    //============================================================================================
    // parse options
    //============================================================================================
    try {
        op.parse(argc, argv);
        if (help_option->count() == 1) {
            std::cout << op << std::endl;
            return 0;
        }
    } catch (const popl::invalid_option& e) {
        std::cerr << "Invalid Option Exception: " << e.what() << std::endl;
        return -1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    std::shared_ptr<vx::mcp::ToolProfile> tool_profile;
    try {
        tool_profile = std::make_shared<vx::mcp::ToolProfile>(
            vx::mcp::ToolProfile::Load(tool_profiles_file, tool_profile_name));
    } catch (const std::exception& error) {
        std::cerr << "Unable to load tool profile: " << error.what() << std::endl;
        return -1;
    }

    //============================================================================================
    // setup transport
    //============================================================================================
    if (use_sse_server->is_set()) {
        transport = std::make_shared<vx::transport::SSE>();
    } else {
        transport = std::make_shared<vx::transport::Stdio>();
    }

    //============================================================================================
    // setup logger
    //============================================================================================
    // Get the current time as ISO 8601 string
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H-%M-%S");
    std::string iso_date = ss.str();

    // Concatenate ISO date to logname
    std::string logFilename = logs_directory + "/mcp-server_" + iso_date + ".log";
    auto sink_file = std::make_shared<AixLog::SinkFile>(AixLog::Severity::trace, logFilename);
    AixLog::Log::init({sink_file});

    //============================================================================================
    // print logo and info
    //============================================================================================
    LOG(INFO) << " __  __  _____ _____        _____ ______ _______      ________ _____  " << std::endl;
    LOG(INFO) << "|  \\/  |/ ____|  __ \\      / ____|  ____|  __ \\ \\    / /  ____|  __ \\ " << std::endl;
    LOG(INFO) << "| \\  / | |    | |__) |____| (___ | |__  | |__) \\ \\  / /| |__  | |__) |" << std::endl;
    LOG(INFO) << "| |\\/| | |    |  ___/______\\___ \\|  __| |  _  / \\ \\/ / |  __| |  _  / " << std::endl;
    LOG(INFO) << "| |  | | |____| |          ____) | |____| | \\ \\  \\  /  | |____| | \\ \\ " << std::endl;
    LOG(INFO) << "|_|  |_|\\_____|_|         |_____/|______|_|  \\_\\  \\/   |______|_|  \\_\\" << std::endl;
    LOG(INFO) << "Starting mcp-server v" << PROJECT_VERSION << " (transport: " << transport->GetName() << " v" << transport->GetVersion() << ") on port: " << transport->GetPort() << std::endl;
    LOG(INFO) << "Press Ctrl+C to exit." << std::endl;

    //============================================================================================
    // load all plugins from the plugins directory
    //============================================================================================
    PluginHostAPI host_api{
        MCP_PLUGIN_ABI_VERSION,
        sizeof(PluginHostAPI),
        nullptr,
        ClientNotificationCallbackImpl
    };
    std::string registry_error;
    vx::mcp::PluginRuntimeConfig runtime_config;
    runtime_config.source_directory = plugins_directory;
    runtime_config.staging_directory = plugin_staging_directory;
    runtime_config.host_api = host_api;
    runtime_config.debounce = std::chrono::milliseconds(
        std::max(0, plugin_debounce_ms));
    runtime_config.delete_grace = std::chrono::milliseconds(
        std::max(0, plugin_delete_grace_ms));
    runtime_config.retain_generations = static_cast<std::size_t>(
        std::max(1, plugin_retain_generations));
    runtime_config.capability_notification = [](const std::string& method) {
        nlohmann::json notification{
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        const std::string payload = notification.dump();
        ClientNotificationCallbackImpl(
            nullptr, "plugin-runtime", payload.c_str());
    };
    if (!plugin_runtime->Initialize(runtime_config, &registry_error)) {
        LOG(ERROR) << "Failed to initialize plugin runtime: "
                   << registry_error << std::endl;
        return -1;
    }
    LOG(INFO) << "Successfully initialized plugin runtime at generation "
              << plugin_runtime->Snapshot()->Generation() << std::endl;
    if (plugin_hot_reload->is_set() &&
        !plugin_runtime->StartWatching(&registry_error)) {
        LOG(ERROR) << "Failed to start plugin hot reload: "
                   << registry_error << std::endl;
        return -1;
    }

    //============================================================================================
    // start server
    //============================================================================================
    server->Name(name);
    server->VerboseLevel(verbose ? 1 : 0);
    server->OverrideCallback("tools/list", [plugin_runtime, tool_profile](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["tools"] = json::array();

        for (const auto& route : registry->Tools()) {
            if (!tool_profile->Allows(route.name)) continue;
            response["result"]["tools"].push_back({
                {"name", route.name},
                {"description", route.description},
                {"inputSchema", route.input_schema}
            });
        }

        return response;
    });
    server->OverrideCallback("tools/call", [plugin_runtime, tool_profile](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        const std::string tool_name = request["params"]["name"];
        if (!tool_profile->Allows(tool_name)) {
            response["result"]["isError"] = true;
            response["result"]["content"] = json::array({{
                {"type", "text"},
                {"text", "Tool is not available in profile: " + tool_profile->Name()}
            }});
            return response;
        }
        const vx::mcp::ToolRoute* route = registry->FindTool(tool_name);
        if (route) {
            const auto plugin = route->plugin;
            char* res_ptr = plugin->Api().HandleRequest(request.dump().c_str());
            if (res_ptr) {
                try {
                    response["result"] = json::parse(res_ptr);
                    if (!response["result"].contains("isError")) {
                        response["result"]["isError"] = false;
                    }
                } catch (const json::parse_error&) {
                    response["result"]["isError"] = true;
                    response["result"]["content"] = json::array();
                    response["result"]["content"].push_back({
                        {"type", "text"}, {"text", "Plugin returned malformed data."}
                    });
                }
                plugin->FreeResult(res_ptr);
            } else {
                LOG(ERROR) << "Plugin " << tool_name
                           << " returned nullptr." << std::endl;
            }
            return response;
        }

        // 未找到匹配的工具，返回错误
        response["result"]["isError"] = true;
        response["result"]["content"] = json::array();
        response["result"]["content"].push_back({
            {"type", "text"},
            {"text", "Tool not found: " + tool_name}
        });
        return response;
    });
    server->OverrideCallback("prompts/list", [plugin_runtime](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["prompts"] = json::array();

        for (const auto& route : registry->Prompts()) {
            response["result"]["prompts"].push_back({
                {"name", route.name},
                {"description", route.description},
                {"arguments", route.arguments}
            });
        }

        return response;
    });
    server->OverrideCallback("prompts/get", [plugin_runtime](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        const std::string prompt_name = request["params"]["name"];
        const vx::mcp::PromptRoute* route = registry->FindPrompt(prompt_name);
        if (route) {
            const auto plugin = route->plugin;
            char* res_ptr = plugin->Api().HandleRequest(request.dump().c_str());
            if (res_ptr) {
                try {
                    response["result"] = json::parse(res_ptr);
                } catch (const json::parse_error&) {
                    LOG(ERROR) << "Plugin " << prompt_name
                               << " returned malformed data." << std::endl;
                }
                plugin->FreeResult(res_ptr);
            }
        }

        return response;
    });
    server->OverrideCallback("resources/list", [plugin_runtime](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);
        response["result"]["resources"] = json::array();

        for (const auto& route : registry->Resources()) {
            response["result"]["resources"].push_back({
                {"name", route.name},
                {"description", route.description},
                {"uri", route.uri},
                {"mimeType", route.mime_type}
            });
        }

        return response;
    });
    server->OverrideCallback("resources/read", [plugin_runtime](const json& request) {
        const auto registry = plugin_runtime->Snapshot();
        nlohmann::ordered_json response = MCPBuilder::Response(request);

        const std::string uri = request["params"]["uri"];
        const vx::mcp::ResourceRoute* route = registry->FindResource(uri);
        if (route) {
            const auto plugin = route->plugin;
            char* res_ptr = plugin->Api().HandleRequest(request.dump().c_str());
            if (res_ptr) {
                try {
                    response["result"] = json::parse(res_ptr);
                } catch (const json::parse_error&) {
                    LOG(ERROR) << "Plugin resource " << route->name
                               << " returned malformed data." << std::endl;
                }
                plugin->FreeResult(res_ptr);
            }
        }

        return response;
    });

    server->Connect(transport);
    plugin_runtime->StopWatching();

    return 0;
}
