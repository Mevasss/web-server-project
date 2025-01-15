#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 80

std::vector<std::pair<std::string, std::string>> chat_history; 
std::unordered_map<std::string, std::string> user_nicknames;
std::mutex chat_mutex;

std::string read_html_template();

std::string generate_html(const std::string& client_ip) {
    std::string template_html = read_html_template();
    std::string chat_content;
    std::string nickname_value;

    chat_mutex.lock();
    for (const auto& [nickname, message] : chat_history) {
        if (user_nicknames[client_ip] == nickname) {
            chat_content += "<div style=\"text-align: right; margin-bottom: 20px;\">";
            chat_content += "<span style=\"font-size: 14px; color: #4caf50; margin-bottom: 5px; display: block;\">" + nickname + "</span>";
            chat_content += "<span style=\"background-color: #d1e7dd; padding: 10px; border-radius: 10px; display: inline-block;\">" + message + "</span></div>";
        }
        else {
            chat_content += "<div style=\"text-align: left; margin-bottom: 20px;\">";
            chat_content += "<span style=\"font-size: 14px; color: #6200ea; margin-bottom: 5px; display: block;\">" + nickname + "</span>";
            chat_content += "<span style=\"background-color: #f8d7da; padding: 10px; border-radius: 10px; display: inline-block;\">" + message + "</span></div>";
        }
    }

    if (user_nicknames.find(client_ip) != user_nicknames.end()) {
        nickname_value = user_nicknames[client_ip];
    }
    chat_mutex.unlock();

    std::size_t pos = template_html.find("{{chat}}");
    if (pos != std::string::npos) {
        template_html.replace(pos, 8, chat_content);
    }

    pos = template_html.find("{{nickname}}");
    if (pos != std::string::npos) {
        template_html.replace(pos, 12, nickname_value);
    }

    return template_html;
}

std::string read_html_template() {
    std::ifstream file("template.html");
    if (!file.is_open()) {
        return "<html><body><h1>Error: Unable to load template</h1></body></html>";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return buffer.str();
}
std::string url_decode(const std::string& encoded) {
    std::string decoded;
    char ch;
    int i, ii;
    for (i = 0; i < encoded.length(); i++) {
        if (int(encoded[i]) == 37) { 
            sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            decoded += ch;
            i += 2;
        }
        else if (encoded[i] == '+') {
            decoded += ' ';
        }
        else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

void save_nicknames() {
    std::ofstream file("nicknames.txt");
    if (file.is_open()) {
        for (const auto& pair : user_nicknames) {
            file << pair.first << ":" << pair.second << "\n";
        }
        file.close();
    }
}

void load_nicknames() {
    std::ifstream file("nicknames.txt");
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::size_t pos = line.find(":");
            if (pos != std::string::npos) {
                std::string ip = line.substr(0, pos);
                std::string nickname = line.substr(pos + 1);
                user_nicknames[ip] = nickname;
            }
        }
        file.close();
    }
}

void handle_client(int client_socket) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    read(client_socket, buffer, sizeof(buffer) - 1);

    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_socket, (struct sockaddr*)&addr, &addr_len);
    std::string client_ip = inet_ntoa(addr.sin_addr);

    std::string request(buffer);
    if (request.substr(0, 3) == "GET") {
        std::string content = generate_html(client_ip);
        std::string response = "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: " + std::to_string(content.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + content;
        send(client_socket, response.c_str(), response.size(), 0);
    }
    else if (request.substr(0, 4) == "POST") {
        std::size_t nick_pos = request.find("nickname=");
        std::size_t msg_pos = request.find("message=");
        std::string nickname, message;

        if (nick_pos != std::string::npos) {
            nickname = request.substr(nick_pos + 9);
            nickname = nickname.substr(0, nickname.find("&"));
            nickname = url_decode(nickname);

            chat_mutex.lock();
            user_nicknames[client_ip] = nickname;
            save_nicknames();
            chat_mutex.unlock();
        }

        if (msg_pos != std::string::npos) {
            message = request.substr(msg_pos + 8);
            message = message.substr(0, message.find("&"));
            message = url_decode(message);

            chat_mutex.lock();
            if (user_nicknames.find(client_ip) != user_nicknames.end()) {
                chat_history.emplace_back(user_nicknames[client_ip], message);

            }
            else {
                chat_history.emplace_back("Anonymous", message);
            }
            chat_mutex.unlock();
        }

        std::string content = "<html><body>Redirecting...</body></html>";
        std::string response = "HTTP/1.1 303 See Other\r\n"
            "Location: /\r\n"
            "Content-Length: " + std::to_string(content.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + content;
        send(client_socket, response.c_str(), response.size(), 0);
    }

    close(client_socket); 
}

int main() {
    load_nicknames();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server running on port " << PORT << "...\n";

    while (true) {
        int client_socket = accept(server_fd, nullptr, nullptr);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        std::thread(handle_client, client_socket).detach();
    }

    close(server_fd);
    return 0;
}
