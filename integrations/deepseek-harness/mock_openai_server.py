#!/usr/bin/env python3
"""Deterministic OpenAI-compatible mock used by Harness tool-loop tests.

The user prompt selects a scenario. The mock then emits a fixed sequence of
native function calls, allowing orchestration/error/large-result behavior to
be tested without API cost or nondeterministic model selection.
"""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import sys
import time


class Handler(BaseHTTPRequestHandler):
    @staticmethod
    def tool_name(request, suffix):
        return next(
            (tool["function"]["name"] for tool in request.get("tools", [])
             if tool["function"]["name"].endswith(f"__{suffix}")),
            f"mcp__gis__{suffix}",
        )

    @staticmethod
    def tool_call(request, suffix, call_id, arguments):
        return {"role": "assistant", "content": None, "tool_calls": [{
            "id": call_id, "type": "function",
            "function": {
                "name": Handler.tool_name(request, suffix),
                "arguments": json.dumps(arguments),
            },
        }]}

    def do_GET(self):
        if self.path == "/v1/models":
            self.send_json(200, {"object": "list", "data": [{"id": "gis-mock", "object": "model"}]})
        else:
            self.send_json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_json(404, {"error": {"message": "not found"}})
            return
        size = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(size))
        messages = request.get("messages", [])
        user_text = "\n".join(
            str(item.get("content", "")) for item in messages
            if item.get("role") == "user"
        )
        tool_results = [item for item in messages if item.get("role") == "tool"]
        point = {"type": "Point", "coordinates": [118.78, 32.06]}

        if "CHAIN_SCENARIO" in user_text and len(tool_results) == 0:
            message = self.tool_call(request, "geometry_validate", "chain-1",
                                     {"geometry": point})
        elif "CHAIN_SCENARIO" in user_text and len(tool_results) == 1:
            message = self.tool_call(request, "geometry_buffer", "chain-2",
                                     {"geometry": point, "distanceMeters": 250})
        elif "ERROR_SCENARIO" in user_text and len(tool_results) == 0:
            # Deliberately invalid CRS exercises isError propagation into the
            # next model turn; the second call corrects the arguments.
            message = self.tool_call(request, "coordinate_transform", "error-1", {
                "coordinate": {"x": 118.78, "y": 32.06},
                "targetCrs": "EPSG:NOT_A_CRS",
            })
        elif "ERROR_SCENARIO" in user_text and len(tool_results) == 1:
            message = self.tool_call(request, "coordinate_transform", "error-2", {
                "coordinate": {"x": 118.78, "y": 32.06},
                "targetCrs": "EPSG:3857",
            })
        elif "LARGE_SCENARIO" in user_text and len(tool_results) == 0:
            message = self.tool_call(request, "geometry_buffer", "large-1",
                                     {"geometry": point, "distanceMeters": 5000,
                                      "quadrantSegments": 32})
        elif "DATASET_SCENARIO" in user_text and len(tool_results) == 0:
            message = self.tool_call(request, "dataset_info", "dataset-1",
                                     {"path": "places.geojson"})
        elif "DATASET_SCENARIO" in user_text and len(tool_results) == 1:
            polygon = {
                "type": "Polygon",
                "coordinates": [[
                    [118.79, 32.05], [118.81, 32.05],
                    [118.81, 32.07], [118.79, 32.07],
                    [118.79, 32.05],
                ]],
            }
            message = self.tool_call(request, "features_within", "dataset-2", {
                "path": "places.geojson", "polygon": polygon,
            })
        elif "HOT_RELOAD_SCENARIO" in user_text and len(tool_results) == 0:
            message = self.tool_call(request, "runtime_tool", "hot-1", {})
        elif "HOT_RELOAD_SCENARIO" in user_text and len(tool_results) == 1:
            # Give the C++ inotify watcher time to publish the new registry and
            # Harness time to process tools/list_changed before the next call.
            marker = os.environ.get("GIS_HOT_RELOAD_MARKER")
            if marker:
                Path(marker).touch()
            time.sleep(2)
            message = self.tool_call(request, "loader_test_tool", "hot-2", {})
        elif len(tool_results) == 0:
            message = self.tool_call(request, "geometry_validate", "basic-1",
                                     {"geometry": point})
        else:
            scenario = "BASIC"
            if "CHAIN_SCENARIO" in user_text: scenario = "CHAIN"
            if "ERROR_SCENARIO" in user_text: scenario = "ERROR"
            if "LARGE_SCENARIO" in user_text: scenario = "LARGE"
            if "DATASET_SCENARIO" in user_text: scenario = "DATASET"
            if "HOT_RELOAD_SCENARIO" in user_text: scenario = "HOT_RELOAD"
            message = {"role": "assistant",
                       "content": f"MOCK_GIS_{scenario}_COMPLETE"}
        finish_reason = "tool_calls" if "tool_calls" in message else "stop"
        response = {
            "id": "mock-completion", "object": "chat.completion",
            "created": 0, "model": "gis-mock",
            "choices": [{"index": 0, "message": message,
                         "finish_reason": finish_reason}],
            "usage": {"prompt_tokens": 1, "completion_tokens": 1, "total_tokens": 2},
        }
        if request.get("stream"):
            delta = dict(message)
            delta.pop("role", None)
            chunk = {
                "id": response["id"], "object": "chat.completion.chunk",
                "created": 0, "model": "gis-mock",
                "choices": [{"index": 0, "delta": delta,
                             "finish_reason": finish_reason}],
            }
            body = (f"data: {json.dumps(chunk, ensure_ascii=False)}\n\n"
                    "data: [DONE]\n\n").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_json(200, response)

    def send_json(self, status, value):
        payload = json.dumps(value, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 18091
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
