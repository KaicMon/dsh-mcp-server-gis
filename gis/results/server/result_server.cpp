#include "gis/results/memory_result_store.h"

#include "httplib.h"
#include "json.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

void Cors(httplib::Response& response) {
    // The development default is loopback-only. A remote deployment must set
    // an explicit origin at its reverse proxy instead of widening the bind.
    response.set_header("Access-Control-Allow-Origin", "*");
    response.set_header("Cache-Control", "no-store");
}

void JsonResponse(httplib::Response& response, int status, const json& body) {
    Cors(response);
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

std::string MapPage(const std::string& result_id) {
    // result_id is constrained by the route regex, so interpolation cannot
    // inject script. Leaflet is loaded from a CDN only for the local demo.
    return R"HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GIS MCP Result</title>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<style>html,body,#map{height:100%;margin:0}#status{position:absolute;z-index:1000;top:10px;left:50px;background:white;padding:8px;border-radius:4px}</style>
</head><body><div id="status">正在加载地图结果…</div><div id="map"></div>
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script><script>
const map=L.map('map').setView([32.06,118.79],11);
L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:19,attribution:'© OpenStreetMap'}).addTo(map);
fetch('/api/v1/results/)HTML" + result_id + R"HTML(').then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return r.json()})
.then(data=>{const layer=L.geoJSON(data).addTo(map);if(layer.getBounds().isValid())map.fitBounds(layer.getBounds(),{padding:[20,20]});document.getElementById('status').remove()})
.catch(e=>document.getElementById('status').textContent='加载失败：'+e.message);
</script></body></html>)HTML";
}

}  // namespace

int main(int argc, char** argv) {
    // Keep the default distinct from routing_http (8088). On WSL, binding
    // 0.0.0.0 and 127.0.0.1 to the same port can work inside Linux but Windows
    // localhost forwarding may route both URLs to the wildcard listener.
    const int port = argc >= 2 ? std::stoi(argv[1]) : 18096;
    const std::string bind = argc >= 3 ? argv[2] : "127.0.0.1";
    const std::string public_base = argc >= 4 ? argv[3]
        : "http://127.0.0.1:" + std::to_string(port);

    gis::results::MemoryResultStore store;
    httplib::Server server;
    server.set_payload_max_length(8U * 1024U * 1024U);

    server.Get("/api/v1/status", [&](const auto&, auto& response) {
        JsonResponse(response, 200,
            {{"status", "ready"}, {"store", "memory"},
             {"resultCount", store.Size()}, {"totalBytes", store.TotalBytes()}});
    });
    server.Post("/api/v1/results", [&](const httplib::Request& request,
                                        httplib::Response& response) {
        try {
            long long ttl = 0;
            if (request.has_param("ttlSeconds")) {
                ttl = std::stoll(request.get_param_value("ttlSeconds"));
            }
            const auto stored = store.Put({
                .payload = request.body,
                .content_type = request.get_header_value("Content-Type"),
                .ttl = std::chrono::seconds(ttl),
            });
            if (!stored.Ok()) {
                JsonResponse(response, 422,
                    {{"error", "RESULT_REJECTED"},
                     {"message", stored.error.message}});
                return;
            }
            JsonResponse(response, 201,
                {{"resultId", stored.value->id},
                 {"mapUrl", public_base + "/results/" + stored.value->id}});
        } catch (const std::exception& error) {
            JsonResponse(response, 400,
                {{"error", "INVALID_REQUEST"}, {"message", error.what()}});
        }
    });
    server.Get(R"(/api/v1/results/(gis_[0-9a-f]{32}))",
        [&](const httplib::Request& request, httplib::Response& response) {
            const auto result = store.Get(request.matches[1]);
            if (!result.Ok()) {
                JsonResponse(response, 404,
                    {{"error", "RESULT_NOT_FOUND"},
                     {"message", result.error.message}});
                return;
            }
            Cors(response);
            response.status = 200;
            response.set_content(result.value->payload,
                result.value->content_type.empty() ? "application/geo+json"
                                                   : result.value->content_type);
        });
    server.Get(R"(/results/(gis_[0-9a-f]{32}))",
        [&](const httplib::Request& request, httplib::Response& response) {
            Cors(response);
            response.set_content(MapPage(request.matches[1]),
                                 "text/html; charset=utf-8");
        });

    std::cout << "GIS result server listening on " << public_base << '\n';
    if (!server.listen(bind, port)) {
        std::cerr << "Unable to listen on " << bind << ':' << port << '\n';
        return 1;
    }
    return 0;
}
