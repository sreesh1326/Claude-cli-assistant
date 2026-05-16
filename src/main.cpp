#include <cstdlib>
#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
using namespace std;

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "-p") {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty()) {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty()) {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }

    // Define tools array
    json tools = json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "Read"},
                {"description", "Read and return the contents of a file"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"file_path", {
                            {"type", "string"},
                            {"description", "The path to the file to read"}
                        }}
                    }},
                    {"required", json::array({"file_path"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "Write"},
                {"description", "Write content to a file"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"file_path", {
                            {"type", "string"},
                            {"description", "The path of the file to write to"}
                        }},
                        {"content", {
                            {"type", "string"},
                            {"description", "The content to write to the file"}
                        }}
                    }},
                    {"required", json::array({"file_path", "content"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "Bash"},
                {"description", "Execute a shell command"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"command", {
                            {"type", "string"},
                            {"description", "The command to execute"}
                        }}
                    }},
                    {"required", json::array({"command"})}
                }}
            }}
        }
    });

    // Define messages array
    json messages = json::array({
        {{"role", "user"}, {"content", prompt}}
    });

    while (true) {
        json request_body = {
            {"model", "anthropic/claude-haiku-4.5"},
            {"messages", messages},
            {"tools", tools}
        };

        cpr::Response response = cpr::Post(
            cpr::Url{base_url + "/chat/completions"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}
            },
            cpr::Body{request_body.dump()}
        );

        if (response.status_code != 200) {
            std::cerr << "HTTP error: " << response.status_code << std::endl;
            return 1;
        }

        json result = json::parse(response.text);

        if (!result.contains("choices") || result["choices"].empty()) {
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }

        // You can use print statements as follows for debugging, they'll be visible when running tests.
        std::cerr << "Logs from your program will appear here!" << std::endl;

        json message = result["choices"][0]["message"];

        if (message.contains("tool_calls") && !message["tool_calls"].empty() && !message["tool_calls"].is_null()) {
            json tool_call = message["tool_calls"][0];
            std::string func_name = tool_call["function"]["name"];

            // Parse arguments — it's a JSON string, so we parse it again
            json arguments = json::parse(tool_call["function"]["arguments"].get<std::string>());

            if (func_name == "Read") {
                std::string file_path = arguments["file_path"].get<std::string>();

                // Read the file
                std::ifstream file(file_path);
                if (!file.is_open()) {
                    std::cerr << "Could not open file: " << file_path << std::endl;
                    return 1;
                }
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string file_content = buffer.str();

                // Add assistant message and tool result to messages for next iteration
                messages.push_back(message);
                messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_call["id"]},
                    {"content", file_content}
                });

            } else if (func_name == "Write") {
                std::string file_path = arguments["file_path"].get<std::string>();
                std::string content = arguments["content"].get<std::string>();

                // Write content to the file
                std::ofstream file(file_path);
                if (!file.is_open()) {
                    std::cerr << "Could not open file for writing: " << file_path << std::endl;
                    return 1;
                }
                
                file << content;
                file.close();

                // Add assistant message and tool result to messages for next iteration
                messages.push_back(message);
                messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_call["id"]},
                    {"content", "Successfully wrote to " + file_path}
                });
            } else if (func_name == "Bash") {
                std::string command = arguments["command"].get<std::string>();

                // Execute the command and capture output
                std::string output;
                std::array<char, 256> buf;
                FILE* pipe = popen(command.c_str(), "r");
                if (!pipe) {
                    std::cerr << "Failed to execute command: " << command << std::endl;
                    return 1;
                }
                while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                    output += buf.data();
                }
                int exit_code = pclose(pipe);

                std::string result_content = "Exit code: " + std::to_string(exit_code) + "\n" + output;

                // Add assistant message and tool result to messages for next iteration
                messages.push_back(message);
                messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_call["id"]},
                    {"content", result_content}
                });
            }
        } else {
            // No tool call — print normal message and exit loop
            std::cout << message["content"].get<std::string>();
            break;
        }
    }

    return 0;
}
