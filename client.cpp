#include "boost/asio/io_context.hpp"
#include<boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>
#include <iterator>
#include <memory>
#include"myconsole.h"
#include"mymsg_tools.h"
#include<nlohmann/json.hpp>
#include<spdlog/spdlog.h>
#include<spdlog/sinks/basic_file_sink.h>
#include<SQLAPI.h>


#include<fstream>
#include<iostream>
#include<set>
#include <sstream>
#include <utility>


using json = nlohmann::json;
using namespace boost;

using Callback = std::function<void(unsigned, const std::string&, const system::error_code&)>;
const int CLEAR_LINES_CNT = 100;

static void clearbuf(asio::streambuf& buf) {
    buf.consume(buf.size());
}

/// Struct that stores a session with the given server
struct Session {
  Session(asio::any_io_executor ex, const std::string& raw_ip, unsigned short port_num,
     const std::string& /*request*/, unsigned int id, Callback callback):
    m_sock(ex),
    m_ep(asio::ip::address::from_string(raw_ip), port_num),
    m_id(id),
    m_callback(std::move(callback)),
    m_was_cancelled(false) {
      clearbuf(m_sign_choice_request);
      clearbuf(m_login_request_buf);
      clearbuf(m_login_validity_buf);
      clearbuf(m_password_request_buf);
      clearbuf(m_password_validity_response);
      clearbuf(m_action_choice);
      clearbuf(m_action_buf);
      clearbuf(m_chat_buf);
      clearbuf(m_received_message);
    }  

  asio::ip::tcp::socket m_sock; //!< The socket for the client application to connect to the server
  asio::ip::tcp::endpoint m_ep; //!< The server's endpoint
  std::string current_chat;
  std::string login;
  std::string password; 
  SignChoice sign_choice;
  ActionChoice action_choice;

  asio::streambuf m_login_request_buf;
  asio::streambuf m_login_validity_buf;
  asio::streambuf m_sign_choice_request;
  asio::streambuf m_password_request_buf;
  asio::streambuf m_password_validity_response;
  asio::streambuf m_action_choice;
  asio::streambuf m_action_buf;
  asio::streambuf m_chat_buf;
  asio::streambuf m_received_message;
      
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
    initLogger(); 
  } 

  ~AsyncTCPClient() {
    close();
  }

  void clear_console() {
    /// Cross-platform version of clearing the console
    m_console.write(std::string(CLEAR_LINES_CNT, '\n'));
  } 

  void initLogger() {
    /// Initializes a spdlog file logger and sets the logging pattern
    try {
      logger = spdlog::basic_logger_mt("basic_logger",
              "client_log" + std::to_string(getpid()) + ".txt");
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

    auto session = std::make_shared<Session>(io_strand, raw_ip, port_num, "", request_id, callback);
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
    m_work.reset();
    m_ios.join();
  } 
     
private:
  void onConnected(const system::error_code& ec, std::shared_ptr<Session> session) {
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
  asio::async_read_until(session->m_sock, session->m_sign_choice_request, '\n', 
    [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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
    {
      std::istream istrm(&session->m_sign_choice_request);
      std::string sign_choice_request;
      std::getline(istrm, sign_choice_request);
      m_console.write(sign_choice_request); 
    }
    
    // Reseting streambuf
    clearbuf(session->m_sign_choice_request);

    post(ui_strand, [this, session] {
      // Inspecting the input format. It must be 1 or 2
      std::string temp = m_console.read();
      while (temp != "1" && temp != "2") {
        m_console.write("Input incorrect. Enter 1 or 2: ");
        temp = m_console.read();
      } 

      session->sign_choice = static_cast<SignChoice>(std::stoi(temp));

      // Sending the choice to the server
      asio::async_write(session->m_sock, asio::buffer(temp.append("\n")), 
        [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
          onSignUpResponseSent(ec, session);
        }); 
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
    asio::async_read_until(session->m_sock, session->m_login_request_buf, '\n', 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
        onLoginRequestReceived(ec, session);
      });
  }

  bool login_format_is_valid(const std::string& /*login*/) const noexcept {
    /// Inspects the basic client-side login formatting

    return true;
  } 

  void _enter_login(const system::error_code& /*ec*/, std::shared_ptr<Session> session) {
    post(ui_strand, [this, session] {
      while (std::cin >> session->login &&
             !login_format_is_valid(session->login)) {
          std::cout << "Invalid login. Try again: ";
      }

    asio::async_write(session->m_sock, asio::buffer(session->login + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
        onLoginResponseSent(ec, session);
      }); 
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

    {
      std::istream istrm(&session->m_login_request_buf);
      std::string login_request;
      std::getline(istrm, login_request);
      std::cout << login_request;
    }

    clearbuf(session->m_login_request_buf);
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

    asio::async_read_until(session->m_sock, session->m_login_validity_buf, '\n', 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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
    
    LoginValidity login_validity_response = LoginValidity::NOT_REGISTERED;

    {
      std::istream istrm(&session->m_login_validity_buf);
      std::string temp;
      std::getline(istrm, temp);
      login_validity_response = static_cast<LoginValidity>(std::stoi(temp));
    }

    clearbuf(session->m_login_validity_buf);
    
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

  std::string _enter_password(const std::string& request) {
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

    clearbuf(session->m_password_request_buf);

    post(ui_strand, [=] {
        session->password = _enter_password("Enter password: ");
          
        if (session->sign_choice == SIGN_UP) {
          std::string password_repeat;
          do {
            password_repeat = _enter_password("Repeat password: ");
          } while (password_repeat != session->password);
        } 

        asio::async_write(session->m_sock, asio::buffer(session->password + "\n"),
          [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
            onPasswordSent(ec, session);
          });
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

    asio::async_read_until(session->m_sock, session->m_password_validity_response, '\n', 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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
    
    Status password_validity_response;
    {
      std::istream istrm(&session->m_password_validity_response);
      std::string temp;
      std::getline(istrm, temp);
      password_validity_response = static_cast<Status>(std::stoi(temp));
    }

    clearbuf(session->m_password_validity_response);
    
    switch(password_validity_response) {
      case AUTHORIZED: {
        clear_console();

        asio::async_read_until(session->m_sock, session->m_action_choice, '\n', 
          [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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

    {
      std::istream istrm(&session->m_action_choice);
      std::string action_choice_request;
      std::getline(istrm, action_choice_request);
      std::cout << action_choice_request;
    }

    post(ui_strand, [this, session] {
      {
        clearbuf(session->m_action_choice);

        std::string action_choice_temp;

        while (std::cin >> action_choice_temp 
            && action_choice_temp != "1"
            && action_choice_temp != "2")
        {
            std::cout << "Input incorrect. Enter 1 or 2: ";
        }

        session->action_choice = static_cast<ActionChoice>(std::stoi(action_choice_temp));
      }
      
      /// Sends action choice
      asio::async_write(session->m_sock, asio::buffer(std::to_string(session->action_choice) + "\n"), 
        [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
          onActionResponseSent(ec, session);
        }); 
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
    
    asio::async_read_until(session->m_sock, session->m_action_buf, '\n', 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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

    std::string response;
    {
      std::istream istrm(&session->m_action_buf);
      std::getline(istrm, response);
    }
    clearbuf(session->m_action_buf);

    switch (session->action_choice) {
      case LIST_DIALOGS: {
        post(ui_strand, [this, response, session] {
          // Response is expected to contain logins of the client's contacts
          // separated with ' '
          std::istringstream iss(response);
          std::vector<std::string> contacts { std::istream_iterator<std::string>(iss), {} };

          clear_console();

          // set of valid choices
          std::set<std::string> valid_numbers; 
          {
            auto id = 1;
            for (auto& contact: contacts) {
              std::cout << "[" << id << "] " << contact << std::endl;
              valid_numbers.insert(std::to_string(id));
              ++id;
            }
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

          asio::async_write(session->m_sock, asio::buffer(contacts[std::stoi(user_number)-1] + "\n"), 
              [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
              onChatRequestSent(ec, session);
              }); 
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

    asio::async_read_until(session->m_sock, session->m_chat_buf, "\n\n", 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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

    session->current_chat.resize(session->m_chat_buf.size());
    asio::buffer_copy(asio::buffer(session->current_chat), session->m_chat_buf.data());

    clearbuf(session->m_chat_buf);

    asio::async_write(session->m_sock, asio::buffer(std::to_string(READY_TO_CHAT) + "\n"), 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
        onSentReady(ec, session);
      }); 
  }

  void onSentReady(const system::error_code& ec,
                   std::shared_ptr<Session> session) {
      if (ec.value() != 0) {
          session->m_ec = ec;
          onRequestComplete(session);
          return;
      }
      logger->info("'{}' in onSentReady", session->login);

      logger->info("'{}' starting message_wait_loop", session->login);
      asio::async_read_until(
          session->m_sock, session->m_received_message, "\n",
          [this, session](const system::error_code& ec,
                          std::size_t /*bytes_transferred*/) {
              message_wait_loop(ec, session);
          });

       message_send_loop(ec, session);
  }

  void message_send_loop(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Starts loop in the current chat enabling the client to keep sending messages to another party
    if (ec.value() != 0) {
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in message_send_loop", session->login);
    

    post(ui_strand, [this, session] {
      clear_console();
      m_console.write(session->current_chat);
      m_console.write("Write your message: ");

      std::string new_message;
      // We use a do/while loop to prevent empty messages either because of the client input
      // or \n's that were not read before
      do {
        new_message = m_console.read();
      } while (new_message.empty());

      session->current_chat.append(_form_message_str(session->login, new_message));

      asio::async_write(session->m_sock, asio::buffer(new_message + "\n"), 
        [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
          message_send_loop(ec, session);
        }); 
    });
  } 

  void message_wait_loop(const system::error_code& ec, std::shared_ptr<Session> session) {
    /// Starts loop in the current chat enabling the client to keep reading messages from another party

    if (ec.value() != 0) {
      logger->error("'{}' exiting message_wait_loop ({})", session->login, ec.message());
      session->m_ec = ec;
      onRequestComplete(session);
      return;
    }
    logger->info("'{}' in message_wait_loop", session->login);

    std::string received_message;
    {
      std::istream istrm(&session->m_received_message);
      std::getline(istrm, received_message);
    }

    clearbuf(session->m_received_message);

    session->current_chat.append(received_message + "\n");

    clear_console();
    m_console.write(session->current_chat);
    m_console.write("Write your message: ");
    

    asio::async_read_until(session->m_sock, session->m_received_message, "\n", 
      [this, session] (const system::error_code& ec, std::size_t /*bytes_transferred*/) {
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
  using executor_type = asio::thread_pool::executor_type;
  asio::thread_pool m_ios { 2 }; // two io threads

  using work_guard = asio::executor_work_guard<executor_type>;
  work_guard m_work { m_ios.get_executor() };

  using strand = asio::strand<executor_type>;
  strand ui_strand { m_ios.get_executor() };
  strand io_strand { m_ios.get_executor() };

  std::map<int, std::shared_ptr<Session>> m_active_sessions;
  std::mutex m_active_sessions_guard;
  std::shared_ptr<spdlog::logger> logger;

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
  std::ifstream cfg_istrm("./cfg_client.json");
  json cfg = json::parse(cfg_istrm);

  std::string raw_ip = cfg["server_ip"];
  unsigned short port_num = cfg["server_port"];

  try {
    AsyncTCPClient client;

    client.connect(raw_ip, port_num, handler, 1);
    //client.emulate(10, raw_ip, port_num, handler, 1);
    std::this_thread::sleep_for(500s);
  } 
  catch (system::system_error& e) {
    std::cout << e.code() << " " << e.what();
  } 
} 
