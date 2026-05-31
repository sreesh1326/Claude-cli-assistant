#include <cstdlib>
#include <iostream>
#include <string>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
using namespace std;
using namespace cpr;

using json = nlohmann::json;

int main(int argc, char* argv[]) {
    if (argc < 3 || string(argv[1]) != "-p") {
        cerr << "Expected first argument to be '-p'" << endl;
        return 1;
    }

    string prompt = argv[2];

    if (prompt.empty()) {
        cerr << "Prompt must not be empty" << endl;
        return 1;
    }

    const char* api_key_env = getenv("OPENROUTER_API_KEY");
    const char* base_url_env = getenv("OPENROUTER_BASE_URL");
    const char* model_env = getenv("OPENROUTER_MODEL");

    string api_key = api_key_env ? api_key_env : "";
    string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";
    string model = model_env ? model_env : "meta-llama/llama-3.2-3b-instruct:free";

    if (api_key.empty()) {
        cerr << "OPENROUTER_API_KEY is not set" << endl;
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
            {"model", model},
            {"messages", messages},
            {"tools", tools}
        };

        Response response = Post(
            Url{base_url + "/chat/completions"},
            Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}
            },
            Body{request_body.dump()}
        );

        if (response.status_code != 200) {
            cerr << "HTTP error: " << response.status_code << endl;
            cerr << response.text << endl;
            return 1;
        }

        json result = json::parse(response.text);

        if (!result.contains("choices") || result["choices"].empty()) {
            cerr << "No choices in response" << endl;
            return 1;
        }

        // You can use print statements as follows for debugging, they'll be visible when running tests.
        cerr << "Logs from your program will appear here!" << endl;

        json message = result["choices"][0]["message"];

        if (message.contains("tool_calls") && !message["tool_calls"].empty() && !message["tool_calls"].is_null()) {
            json tool_call = message["tool_calls"][0];
            string func_name = tool_call["function"]["name"];

            // Parse arguments — it's a JSON string, so we parse it again
            json arguments = json::parse(tool_call["function"]["arguments"].get<string>());

            if (func_name == "Read") {
                string file_path = arguments["file_path"].get<string>();

                // Read the file
                ifstream file(file_path);
                if (!file.is_open()) {
                    cerr << "Could not open file: " << file_path << endl;
                    return 1;
                }
                stringstream buffer;
                buffer << file.rdbuf();
                string file_content = buffer.str();

                // Add assistant message and tool result to messages for next iteration
                messages.push_back(message);
                messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_call["id"]},
                    {"content", file_content}
                });

            } else if (func_name == "Write") {
                string file_path = arguments["file_path"].get<string>();
                string content = arguments["content"].get<string>();

                // Write content to the file
                ofstream file(file_path);
                if (!file.is_open()) {
                    cerr << "Could not open file for writing: " << file_path << endl;
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
                string command = arguments["command"].get<string>();

                // Execute the command and capture output
                string output;
                char buf[256];
                FILE* pipe = popen(command.c_str(), "r");
                if (!pipe) {
                    cerr << "Failed to execute command: " << command << endl;
                    return 1;
                }
                while (fgets(buf, sizeof(buf), pipe) != nullptr) {
                    output += buf;
                }
                int exit_code = pclose(pipe);

                string result_content = "Exit code: " + to_string(exit_code) + "\n" + output;

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
            cout << message["content"].get<string>();
            break;
        }
    }

    return 0;
}
