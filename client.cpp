#include<boost/asio.hpp>
#include<myconsole.h>
#include<mymsg_tools.h>
#include<nlohmann/json.hpp>
#include<spdlog/spdlog.h>
#include<spdlog/sinks/basic_file_sink.h>
#include<SQLAPI.h>


#include<iostream>
#include<fstream>
#include<set>

using json = nlohmann::json;
using namespace boost;

typedef void(*Callback) (unsigned int request_id, const std::string& response, const system::error_code& ec);
const int CLEAR_LINES_CNT = 100;

/// Struct that stores a session with the given server
struct Session {
  Session(asio::io_context& ios, const std::string& raw_ip, unsigned short port_num,
     const std::string& /*request*/, unsigned int id, Callback callback):
    m_sock(ios),
    m_ep(asio::ip::address::from_string(raw_ip), port_num),
    m_id(id),
    m_callback(callback),
    m_was_cancelled(false) {
      m_sign_choice_request.reset(new asio::streambuf);
      m_login_request_buf.reset(new asio::streambuf);
      m_login_validity_buf.reset(new asio::streambuf);
      m_password_request_buf.reset(new asio::streambuf);
      m_password_validity_response.reset(new asio::streambuf);
      m_action_choice.reset(new asio::streambuf);
      m_action_buf.reset(new asio::streambuf);
      m_chat_buf.reset(new asio::streambuf);
      m_received_message.reset(new asio::streambuf);
    }  

  asio::ip::tcp::socket m_sock; //!< The socket for the client application to connect to the server
  asio::ip::tcp::endpoint m_ep; //!< The server's endpoint
  std::string current_chat;
  std::string login;
  std::string password; 
  SignChoice sign_choice;
  ActionChoice action_choice;

  std::shared_ptr<asio::streambuf> m_login_request_buf;
  std::shared_ptr<asio::streambuf> m_login_validity_buf;
  std::shared_ptr<asio::streambuf> m_sign_choice_request;
  std::shared_ptr<asio::streambuf> m_password_request_buf;
  std::shared_ptr<asio::streambuf> m_password_validity_response;
  std::shared_ptr<asio::streambuf> m_action_choice;
  std::shared_ptr<asio::streambuf> m_action_buf;
  std::shared_ptr<asio::streambuf> m_chat_buf;
  std::shared_ptr<asio::streambuf> m_received_message;
      
  std::string m_response;
  system::error_code m_ec; //!< Error object of the session

  unsigned int m_id;
  Callback m_callback;

  bool m_was_cancelled;
  std::mutex m_cancel_guard;
};

/// Class that implements an asynchronous TCP client to interact with Service class
class AsyncTCPClient: public asio::noncopyable {
public:
  AsyncTCPClient() {
    /// Initializes logger, spawns a thread, allocates OS resources and 
    /// starts a loop by using asio::io_context

    initLogger(); 
    m_work.reset(new asio::io_context::work(m_ios));
    m_thread.reset(new std::thread(
          [this]() {
            m_ios.run();
          }));
  } 

  void clear_console() {
    /// Cross-platform version of clearing the console
    m_console.write(std::string(CLEAR_LINES_CNT, '\n'));
  } 

  void initLogger() {
    /// Initializes a spdlog file logger and sets the logging pattern
    try {
      logger = spdlog::basic_logger_mt("basig_logger", "../logs/client_log.txt");
    }
    catch (const spdlog::spdlog_ex& ex) {
      logger = spdlog::default_logger();

      std::cout << "Log init failed: " << ex.what() << std::endl;
    }
    logger->set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v");
    logger->flush_on(spdlog::level::info);
  }

  void connect(const std::string& raw_ip, unsigned short port_num, Callback callback, unsigned int request_id ) {
    /// Connects session's socket to the server's endpoint

    std::shared_ptr<Session> session(new Session(m_ios, raw_ip, port_num, "", request_id, callback));
    session->m_sock.open(session->m_ep.protocol());

    std::unique_lock<std::mutex> lock(m_active_sessions_guard);
    m_active_sessions[request_id] = session;
    lock.unlock();
  
    session->m_sock.async_connect(session->m_ep,
      [this, session](const system::error_code& ec) {
        onConnected(ec, session);
      });  
  }

  void cancelRequest(unsigned int request_id) {
    std::unique_lock<std::mutex> lock(m_active_sessions_guard);

    auto it = m_active_sessions.find(request_id);
    if (it != m_active_sessions.end()) {
      std::unique_lock<std::mutex> cancel_lock(it->second->m_cancel_guard);
      it->second->m_sock.cancel();
    } 
  } 

  void close() {
    m_work.reset(NULL);
    m_thread->join();
  } 
     
private:
  void onConnected(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
  asio::async_read_until(session->m_sock, *(session->m_sign_choice_request.get()), '\n', 
    [this, session] (const system::error_code& ec, std::size_t) {
        onSignUpRequestReceived(ec, session);
      });
  }

  void onSignUpRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    // Reading the server's request for a choice between sign up and sign in
    std::istream istrm(session->m_sign_choice_request.get());
    std::string sign_choice_request;
    std::getline(istrm, sign_choice_request);
    m_console.write(sign_choice_request); 
    
    // Reseting streambuf
    session->m_sign_choice_request.reset(new asio::streambuf);

    // Inspecting the input format. It must be 1 or 2
    std::string temp = m_console.read();
    while (temp != "1" && temp != "2") {
      m_console.write("Input incorrect. Enter 1 or 2: ");
      temp = m_console.read();
    } 

    session->sign_choice = static_cast<SignChoice>(std::stoi(temp));

    // Sending the choice to the server
    asio::async_write(session->m_sock, asio::buffer(temp.append("\n")), 
      [this, session] (const system::error_code& ec, std::size_t) {
        onSignUpResponseSent(ec, session);
      }); 
  } 

  void onSignUpResponseSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onSignUpResponseSent");
    
    // Reading a login request from the server 
    asio::async_read_until(session->m_sock, *(session->m_login_request_buf.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onLoginRequestReceived(ec, session);
      });
  }

  bool login_format_is_valid(const std::string& /*login*/) const noexcept {
    /// Inspects the basic client-side login formatting

    return true;
  } 

  void _enter_login(const system::error_code& /*ec*/, std::shared_ptr<Session> session) {
    std::cin >> session->login;
    while (!login_format_is_valid(session->login)) {
      std::cout << "Invalid login. Try again: ";
      std::cin >> session->login;
    } 

    asio::async_write(session->m_sock, asio::buffer(session->login + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t) {
        onLoginResponseSent(ec, session);
      }); 
  }  

  void onLoginRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Sends login to the server

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginRequestReceived");

    std::istream istrm(session->m_login_request_buf.get());
    std::string login_request;
    std::getline(istrm, login_request);
    std::cout << login_request;

    session->m_login_request_buf.reset(new asio::streambuf);
    _enter_login(ec, session);
      
  } 
  
  void onLoginResponseSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Receives a login validity response from the server 

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginResponseSent");

    asio::async_read_until(session->m_sock, *(session->m_login_validity_buf.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onLoginValidityResponseReceived(ec, session);
      });
  } 

  void onLoginValidityResponseReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Processes login validity response 
    
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onLoginValidityResponseReceived");
    
    std::istream istrm(session->m_login_validity_buf.get());
    std::string temp;
    std::getline(istrm, temp);
    LoginValidity login_validity_response = static_cast<LoginValidity>(std::stoi(temp));

    session->m_login_validity_buf.reset(new asio::streambuf);
    
    /// No other user should have the same login if the choice is to sign up.
    /// The login must be registered if the choice is to sign in. The server checks it
    /// and the client is asked to try again if login is invalid 
    
    switch (login_validity_response) {  
      case IS_TAKEN: {
        std::cout << "Login is taken. Enter unique login: ";
        _enter_login(ec, session);
        break;
      } 
      case NOT_REGISTERED: {
        std::cout << "No such login registered. Enter login: ";
        _enter_login(ec, session);
        break;
      } 
      case IS_VALID: {
        onPasswordRequestReceived(ec, session);
        break;
      } 
    } 
  } 

  bool password_format_is_valid(const std::string& password) const noexcept {
    /// Inspects basic client-side password formatting
    return !password.empty();
  }

  std::string _enter_password(const std::string& request, const system::error_code& /*ec*/, std::shared_ptr<Session> /*session*/) {
    std::string res;
    std::cout << request;
    std::cin >> res;
    while (!password_format_is_valid(res)) {
      std::cout << "Invalid password. Try again: ";
      std::cin >> res;
    } 
    return res;
  } 

  void onPasswordRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Processes password request. Client is prompted to enter password.
    /// Sign up. Client is prompted to repeat password

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onPasswordRequestReceived");

    session->m_password_request_buf.reset(new asio::streambuf);

    session->password = _enter_password("Enter password: ", ec, session);
      
    if (session->sign_choice == SIGN_UP) {
      std::string password_repeat;
      do {
        password_repeat = _enter_password("Repeat password: ", ec, session);
      } while (password_repeat != session->password);
    } 

    asio::async_write(session->m_sock, asio::buffer(session->password + "\n"),
      [this, session] (const system::error_code& ec, std::size_t) {
        onPasswordSent(ec, session);
      });
  } 

  void onPasswordSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Reads password validity response 

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onPasswordSent");

    asio::async_read_until(session->m_sock, *(session->m_password_validity_response.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onPasswordValidityResponseReceived(ec, session);
      });
  } 

  void onPasswordValidityResponseReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Processes password validity response
    
    /// It initiates an action chain depending on the validity response. 
    /// Client is asked to enter login and password again if the attempt to log in was not authorized.
    /// Client is asked to repeat password in case of signing up.

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onPasswordValidityResponseReceived");
    
    std::istream istrm(session->m_password_validity_response.get());
    Status password_validity_response;
    std::string temp;
    std::getline(istrm, temp);

    password_validity_response = static_cast<Status>(std::stoi(temp));
    session->m_password_validity_response.reset(new asio::streambuf);
    
    switch(password_validity_response) {
      case AUTHORIZED: {
        clear_console();

        asio::async_read_until(session->m_sock, *(session->m_action_choice.get()), '\n', 
          [this, session] (const system::error_code& ec, std::size_t) {
            onActionRequestReceived(ec, session);
          });
        break;
      }
      case WRONG_LOG_PASS: {
        // Authorization process begins from scratch. The client is asked to enter their login.
        std::cout << "Wrong login or password. Enter login: ";
        _enter_login(ec, session);
        break;
      }
      default: 
        return;
    } 
  } 

  void onActionRequestReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Processes action request. Prompts the user to enter valid action choice

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onActionRequestReceived");

    std::istream istrm(session->m_action_choice.get());
    std::string action_choice_request;
    std::getline(istrm, action_choice_request);
    std::cout << action_choice_request;

    session->m_action_choice.reset(new asio::streambuf);

    std::string action_choice_temp;
    std::cin >> action_choice_temp;
    while (action_choice_temp != "1" && action_choice_temp != "2") {
      std::cout << "Input incorrect. Enter 1 or 2: ";
      std::cin >> action_choice_temp;
    } 

    session->action_choice = static_cast<ActionChoice>(std::stoi(action_choice_temp));
    
    /// Sends action choice
    asio::async_write(session->m_sock, asio::buffer(std::to_string(session->action_choice) + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t) {
        onActionResponseSent(ec, session);
      }); 
  } 

  void onActionResponseSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Reads action response

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onActionResponseSent");
    
    asio::async_read_until(session->m_sock, *(session->m_action_buf.get()), '\n', 
      [this, session] (const system::error_code& ec, std::size_t) {
        onActionResponseReceived(ec, session);
      });
  } 

  void onActionResponseReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Processes action response
    
    /// Either lists dialogs or prompts the client to enter a login of a new contact
    /// Then it starts chat with the new contact
     
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onActionResponseReceived");

    std::istream istrm(session->m_action_buf.get());
    std::string response;
    std::getline(istrm, response);

    int cnt = 1;
    std::map<int, std::string> contacts; // map to store user's number in the list and login
    session->m_action_buf.reset(new asio::streambuf);

    switch (session->action_choice) {
      case LIST_DIALOGS: {
        clear_console();
        
        std::string delimeter = " ";
        std::string::size_type pos = 0;
        std::string cur_login; 
        std::set<std::string> valid_numbers; 
        // Response is expected to contain logins of the client's contacts
        // separated with ' '
        while ((pos = response.find(delimeter)) != std::string::npos) {
          valid_numbers.insert(std::to_string(cnt));
          cur_login = response.substr(0, pos);
          std::cout << "[" + std::to_string(cnt) + "] " + cur_login << std::endl;
          response.erase(0, pos + delimeter.length());
          contacts[cnt] = cur_login;
          cnt ++;
        } 
        // Client is asked to enter the user's number in the list of dialogs
        std::cout << "Enter user number: ";
        std::string user_number;
        std::cin >> user_number;
        
        auto it = valid_numbers.find(user_number);
        while (it == valid_numbers.end()) {
          std::cout << "Incorrect number. Try again: ";
          std::cin >> user_number;
          it = valid_numbers.find(user_number);
        } 

        asio::async_write(session->m_sock, asio::buffer(contacts[std::stoi(user_number)] + "\n"), 
          [this, session] (const system::error_code& ec, std::size_t) {
            onChatRequestSent(ec, session);
          }); 
        break;
      }
      case ADD_CONTACT: {
        std::cout << response << std::flush;
        onRequestComplete(session);
        return;
        break;
      }
    } 
  } 

  void onChatRequestSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Reads chat history
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onChatRequestSent");

    asio::async_read_until(session->m_sock, *(session->m_chat_buf.get()), "\n\n", 
      [this, session] (const system::error_code& ec, std::size_t) {
        onCharResponseReceived(ec, session);
      });
  } 

  void onCharResponseReceived(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Displays interactive client's chat with chosen user. The client is then asked to enter new message.

    /// The chat is interactive meaning that if another party sends a message to the client this message  
    /// will appear in the chat.

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in onChatResponseReceived", session->login);

    std::istream istrm(session->m_chat_buf.get());
    session->current_chat = "";
    std::string message;

    // Composing chat from messages
    do {
      std::getline(istrm, message);
      session->current_chat.append(message + "\n");
    } while (!istrm.eof() && !message.empty());  

    // \n in the last message append to current chat is doubled
    session->current_chat.erase(session->current_chat.length() - 1);
    session->m_chat_buf.reset(new asio::streambuf);

    asio::async_write(session->m_sock, asio::buffer(std::to_string(READY_TO_CHAT) + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t) {
        onSentReady(ec, session);
      }); 
  } 

  void onSentReady(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in onSentReady", session->login);

    msg_wait_thread.reset(new std::thread([this, ec, session] {
      asio::async_read_until(session->m_sock, *(session->m_received_message.get()), "\n", 
        [this, session] (const system::error_code& ec, std::size_t) {
          message_wait_loop(ec, session);
        });
      }));
    msg_wait_thread->detach();

    msg_thread.reset(new std::thread([this, ec, session] {
      message_send_loop(ec, session);
      }));

    msg_thread->detach();
  } 


  void message_send_loop(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Starts loop in the current chat enabling the client to keep sending messages to another party
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in message_send_loop", session->login);
    

    clear_console();
    m_console.write(session->current_chat);
    m_console.write("Write your message: ");

    std::string new_message;

    // We use a do/while loop to prevent empty messages either because of the client input
    // or \n's that were not read before

    do {
      new_message = m_console.read();
    } while (new_message.empty());
    

    std::unique_lock<std::mutex> lock_std_out(std_out_guard);
    session->current_chat.append(_form_message_str(session->login, new_message));
    lock_std_out.unlock();

    asio::async_write(session->m_sock, asio::buffer(new_message + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t) {
        message_send_loop(ec, session);
      }); 
  } 

  void message_wait_loop(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Starts loop in the current chat enabling the client to keep reading messages from another party

    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in message_wait_loop", session->login);

    std::istream istrm(session->m_received_message.get());
    std::string received_message;
    std::getline(istrm, received_message);

    session->m_received_message.reset(new asio::streambuf);

    std::unique_lock<std::mutex> lock_std_out(std_out_wait_guard);
    session->current_chat.append(received_message + "\n");
    lock_std_out.unlock();

    clear_console();
    m_console.write(session->current_chat);
    m_console.write("Write your message: ");
    

    asio::async_read_until(session->m_sock, *(session->m_received_message.get()), "\n", 
      [this, session] (const system::error_code& ec, std::size_t) {
        message_wait_loop(ec, session);
      });
  }

  void onMessageSent(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("in onMessageSent");

  } 


private:
  void onRequestComplete(std::shared_ptr<Session> session) {
    logger->info("Completing request");
    system::error_code ignored_ec;

    session->m_sock.shutdown(asio::ip::tcp::socket::shutdown_both, ignored_ec);

    std::unique_lock<std::mutex> lock(m_active_sessions_guard);
    auto it = m_active_sessions.find(session->m_id);
    if (it != m_active_sessions.end()) {
      m_active_sessions.erase(it);
    } 
    lock.unlock();

    system::error_code ec;
    if (session->m_ec.value() == 0 && session->m_was_cancelled) {
      ec = asio::error::operation_aborted; 
    } else {
      ec = session->m_ec;
    } 

    session->m_callback(session->m_id, session->m_response, ec);
  } 

private:
  asio::io_context m_ios;
  std::map<int, std::shared_ptr<Session>> m_active_sessions;
  std::mutex m_active_sessions_guard;
  std::unique_ptr<asio::io_context::work> m_work;
  std::unique_ptr<std::thread> m_thread;
  std::shared_ptr<spdlog::logger> logger;

  std::mutex std_out_guard;
  std::mutex std_out_wait_guard;
  std::unique_ptr<std::thread> msg_thread;
  std::unique_ptr<std::thread> msg_wait_thread;
  Console m_console;
};

void handler(unsigned int /*request_id*/, const std::string& /*response*/, const system::error_code& /*ec*/) {

  /*  
  if (ec.value() == 0) {
    std::cout << "Request #" << request_id << " has completed. Response: " << response << std::endl;
  } else {
    std::cout << "Request #" << request_id << " failed. Error code: "
      << ec.value() << " " << ec.message() << std::endl;
  } 
  */
} 


int main() {
  std::ifstream cfg_istrm("../cfg_client.json");
  json cfg = json::parse(cfg_istrm);

  std::string raw_ip = cfg["server_ip"];
  unsigned short port_num = cfg["server_port"];

  try {
    AsyncTCPClient client;

    client.connect(raw_ip, port_num, handler, 1);
    //client.emulate(10, raw_ip, port_num, handler, 1);
    std::this_thread::sleep_for(std::chrono::seconds(500));
    client.close();
  } 
  catch (system::system_error& e) {
    std::cout << e.code() << " " << e.what();
  } 
} 
