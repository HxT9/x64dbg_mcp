#pragma once

#include <string>

// Dispatches a single JSON request body and returns the JSON response body.
// Request:  {"method":"<name>","params":{...}}
// Response: {"ok":true,"result":...} or {"ok":false,"error":"..."}
std::string HandleRpc(const std::string& body);
