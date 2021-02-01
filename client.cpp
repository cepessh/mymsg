#include "myconsole.h"
#include "mymsg_tools.h"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <cctype>
#include <iterator>
#include <memory>

#include <SQLAPI.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <utility>

using json = nlohmann::json;
namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;

const int CLEAR_LINES_CNT = 100;

static void clearbuf(asio::streambuf& buf) { buf.consume(buf.size()); }

/// Class that implements an asynchronous TCP client to interact with Service
/// class
class ChatSession : public asio::noncopyable {
  void clearbuf(asio::streambuf& buf, bool warn_discard = true)
  {
    if (auto n = buf.size(); warn_discard && n > 0) {
      std::string s(buffers_begin(buf.data()), buffers_end(buf.data()));
      logger->warn("Discarding {} bytes of unused buffer: '{}'", n, s);
    }
    ::clearbuf(buf);
  }

public:
  ChatSession() { initLogger(); }

  ~ChatSession() { close(); }

  void clear_console()
  {
    /// Cross-platform version of clearing the console
    m_console.write(std::string(CLEAR_LINES_CNT, '\n'));
  }

  void initLogger()
  {
    /// Initializes a spdlog file logger and sets the logging pattern
    try {
      logger = spdlog::basic_logger_mt(
          "basic_logger", "client_log" + std::to_string(getpid()) + ".txt");
    } catch (const spdlog::spdlog_ex& ex) {
      logger = spdlog::default_logger();

      std::cout << "Log init failed: " << ex.what() << std::endl;
    }
    logger->set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v");
    logger->flush_on(spdlog::level::info);
  }

  void connect(const std::string& raw_ip, unsigned short port_num)
  {
    /// Connects session's socket to the server's endpoint
    tcp::endpoint ep { asio::ip::address::from_string(raw_ip), port_num };
    m_sock.open(ep.protocol());
    m_sock.async_connect(ep, [this](error_code ec) { onConnected(ec); });
  }

  void close()
  {
    m_work.reset();
    post(io_strand, [this] { m_sock.cancel(); });
    m_ios.join();
  }

private:
  void onConnected(error_code ec)
  {
    if (ec.failed()) {
      return;
    }
    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onSignUpRequestReceived(ec);
        });
  }

  static bool password_format_is_valid(const std::string& password) noexcept
  {
    /// Inspects basic client-side password formatting
    return !password.empty();
  }

  static bool login_format_is_valid(const std::string& login) noexcept
  {
    /// Inspects the basic client-side login formatting
    return !login.empty()
        && std::none_of(begin(login), end(login), [](unsigned char ch) {
             return std::ispunct(ch) || std::isspace(ch) || std::iscntrl(ch);
           });
  }

  void onSignUpRequestReceived(error_code ec)
  {
    if (ec.failed()) {
      return;
    }
    // Reading the server's request for a choice between sign up and sign in
    {
      std::istream istrm(&m_inbuf);
      std::string sign_choice_request;
      std::getline(istrm, sign_choice_request);
      m_console.write(sign_choice_request);
    }

    // Reseting streambuf
    clearbuf(m_inbuf);

    post(ui_strand, [this] {
      // Inspecting the input format. It must be 1 or 2
      std::string temp = m_console.read();
      while (temp != "1" && temp != "2") {
        m_console.write("Input incorrect. Enter 1 or 2: ");
        temp = m_console.read();
      }

      logonState = { static_cast<SignChoice>(std::stoi(temp)), "" };

      // Sending the choice to the server
      m_outbuf = temp + '\n';
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](error_code ec, std::size_t /*bytes_transferred*/) {
            onSignUpResponseSent(ec);
          });
    });
  }

  void onSignUpResponseSent(error_code ec)
  {
    if (ec.failed()) {
      return;
    }
    logger->info("in onSignUpResponseSent");

    // Reading a login request from the server
    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onLoginRequestReceived(ec);
        });
  }

  void _enter_login()
  {
    post(ui_strand, [this]() mutable {
      logonState.login = m_console.read();
      while (!login_format_is_valid(logonState.login)) {
        std::cout << "Invalid login. Try again: ";
        logonState.login = m_console.read();
      }

      m_outbuf = logonState.login + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](error_code ec, std::size_t /*bytes_transferred*/) {
            onLoginResponseSent(ec);
          });
    });
  }

  void onLoginRequestReceived(error_code ec)
  {
    /// Sends login to the server

    logger->info("in onLoginRequestReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    std::cout << &m_inbuf;
    _enter_login();
  }

  void onLoginResponseSent(error_code ec)
  {
    /// Receives a login validity response from the server

    logger->info("in onLoginResponseSent ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onLoginValidityResponseReceived(ec);
        });
  }

  void onLoginValidityResponseReceived(error_code ec)
  {
    /// Processes login validity response

    logger->info("in onLoginValidityResponseReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    LoginValidity login_validity_response = LoginValidity::NOT_REGISTERED;

    {
      std::istream istrm(&m_inbuf);
      std::string temp;
      std::getline(istrm, temp);
      login_validity_response = static_cast<LoginValidity>(std::stoi(temp));
    }

    clearbuf(m_inbuf);

    /// No other user should have the same login if the choice is to sign up.
    /// The login must be registered if the choice is to sign in. The server
    /// checks it and the client is asked to try again if login is invalid

    switch (login_validity_response) {
    case IS_TAKEN: {
      std::cout << "Login is taken. Enter unique login: ";
      _enter_login();
      break;
    }
    case NOT_REGISTERED: {
      std::cout << "No such login registered. Enter login: ";
      _enter_login();
      break;
    }
    case IS_VALID: {
      onPasswordRequestReceived(ec);
      break;
    }
    }
  }

  std::string _enter_password(const std::string& request)
  {
    std::string res;
    std::cout << request;
    std::cin >> res;
    while (!password_format_is_valid(res)) {
      std::cout << "Invalid password. Try again: ";
      std::cin >> res;
    }
    return res;
  }

  void onPasswordRequestReceived(error_code ec)
  {
    /// Processes password request. Client is prompted to enter password.
    /// Sign up. Client is prompted to repeat password

    logger->info("in onPasswordRequestReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    clearbuf(m_inbuf);

    post(ui_strand, [=] {
      std::string password = _enter_password("Enter password: ");

      if (logonState.sign_choice == SIGN_UP) {
        std::string password_repeat;
        do {
          password_repeat = _enter_password("Repeat password: ");
        } while (password_repeat != password);
      }

      m_outbuf = password + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](error_code ec, std::size_t /*bytes_transferred*/) {
            onPasswordSent(ec);
          });
    });
  }

  void onPasswordSent(error_code ec)
  {
    /// Reads password validity response

    logger->info("in onPasswordSent ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onPasswordValidityResponseReceived(ec);
        });
  }

  void onPasswordValidityResponseReceived(error_code ec)
  {
    /// Processes password validity response

    /// It initiates an action chain depending on the validity response.
    /// Client is asked to enter login and password again if the attempt to log
    /// in was not authorized. Client is asked to repeat password in case of
    /// signing up.

    logger->info("in onPasswordValidityResponseReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    Status password_validity_response;
    {
      std::istream istrm(&m_inbuf);
      std::string temp;
      std::getline(istrm, temp);
      password_validity_response = static_cast<Status>(std::stoi(temp));
    }

    clearbuf(m_inbuf);

    switch (password_validity_response) {
    case AUTHORIZED: {
      logger->set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ ["
          + logonState.login + "] %v");
      clear_console();

      asio::async_read_until(m_sock, m_inbuf, '\n',
          [this](error_code ec, std::size_t /*bytes_transferred*/) {
            onActionRequestReceived(ec);
          });
      break;
    }
    case WRONG_LOG_PASS: {
      logger->set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v");
      // Authorization process begins from scratch. The client is asked to enter
      // their login.
      std::cout << "Wrong login or password. Enter login: ";
      _enter_login();
      break;
    }
    default:
      return;
    }
  }

  void onActionRequestReceived(error_code ec)
  {
    /// Processes action request. Prompts the user to enter valid action choice

    logger->info("in onActionRequestReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    {
      std::istream istrm(&m_inbuf);
      std::string action_choice_request;
      std::getline(istrm, action_choice_request);
      std::cout << action_choice_request;
    }

    post(ui_strand, [this] {
      ActionChoice action_choice;
      {
        clearbuf(m_inbuf);

        std::string action_choice_temp;

        while (std::cin >> action_choice_temp && action_choice_temp != "1"
            && action_choice_temp != "2") {
          std::cout << "Input incorrect. Enter 1 or 2: ";
        }

        action_choice
            = static_cast<ActionChoice>(std::stoi(action_choice_temp));
      }

      /// Sends action choice
      m_outbuf = std::to_string(action_choice) + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this, action_choice](
              error_code ec, std::size_t /*bytes_transferred*/) {
            onActionResponseSent(ec, action_choice);
          });
    });
  }

  void onActionResponseSent(error_code ec, ActionChoice action_choice)
  {
    /// Reads action response

    logger->info("in onActionResponseSent ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, action_choice](
            error_code ec, std::size_t /*bytes_transferred*/) {
          onActionResponseReceived(ec, action_choice);
        });
  }

  void onActionResponseReceived(error_code ec, ActionChoice action_choice)
  {
    /// Processes action response

    /// Either lists dialogs or prompts the client to enter a login of a new
    /// contact Then it starts chat with the new contact

    logger->info("in onActionResponseReceived ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    std::string response;
    {
      std::istream istrm(&m_inbuf);
      std::getline(istrm, response);
    }
    clearbuf(m_inbuf);

    switch (action_choice) {
    case LIST_DIALOGS: {
      post(ui_strand, [this, response] {
        // Response is expected to contain logins of the client's contacts
        // separated with ' '
        std::istringstream iss(response);
        std::vector<std::string> contacts {
          std::istream_iterator<std::string>(iss), {}
        };

        clear_console();

        // set of valid choices
        std::set<std::string> valid_numbers;
        {
          auto id = 1;
          for (auto& contact : contacts) {
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

        m_outbuf = contacts[std::stoi(user_number) - 1] + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this](error_code ec, std::size_t /*bytes_transferred*/) {
              onChatRequestSent(ec);
            });
      });
      break;
    }
    case ADD_CONTACT: {
      std::cout << response << std::flush;
      return;
      break;
    }
    }
  }

  void onChatRequestSent(error_code ec)
  {
    /// Reads chat history
    logger->info("in onChatRequestSent ({})", ec.message());
    if (ec.failed()) {
      return;
    }

    asio::async_read_until(m_sock, m_inbuf, "\n\n",
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onChatResponseReceived(ec);
        });
  }

  void onChatResponseReceived(error_code ec)
  {
    /// Displays interactive client's chat with chosen user. The client is then
    /// asked to enter new message.

    /// The chat is interactive meaning that if another party sends a message to
    /// the client this message will appear in the chat.

    logger->info("in onChatResponseReceived");
    if (ec.failed()) {
      return;
    }

    {
      std::istream istrm(&m_inbuf);
      auto& accum = current_chat;
      accum.clear();
      for (std::string line; getline(istrm, line);) {
        if (line.empty())
          continue;
        accum.append(line);
        accum.append("\n");
      }
    }
    clearbuf(m_inbuf, false);

    m_outbuf = std::to_string(READY_TO_CHAT) + "\n";
    asio::async_write(m_sock, asio::buffer(m_outbuf),
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          onSentReady(ec);
        });
  }

  void onSentReady(error_code ec)
  {
    logger->info("in onSentReady");
    if (ec.failed()) {
      return;
    }

    logger->info("starting message_wait_loop");
    asio::async_read_until(m_sock, m_inbuf, "\n",
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          message_wait_loop(ec);
        });

    message_send_loop(ec);
  }

  void message_send_loop(error_code ec)
  {
    /// Starts loop in the current chat enabling the client to keep sending
    /// messages to another party
    logger->info("in message_send_loop");
    if (ec.failed()) {
      return;
    }

    post(ui_strand, [this] {
      clear_console();
      m_console.write(current_chat);
      m_console.write("Write your message: ");

      std::string new_message;
      // We use a do/while loop to prevent empty messages either because of the
      // client input or \n's that were not read before
      do {
        new_message = m_console.read();
      } while (new_message.empty());

      current_chat.append(_form_message_str(logonState.login, new_message));

      m_outbuf = new_message + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](error_code ec, std::size_t /*bytes_transferred*/) {
            message_send_loop(ec);
          });
    });
  }

  void message_wait_loop(error_code ec)
  {
    /// Starts loop in the current chat enabling the client to keep reading
    /// messages from another party

    logger->info("in message_wait_loop");
    if (ec.failed()) {
      return;
    }

    std::string received_message;
    {
      std::istream istrm(&m_inbuf);
      std::getline(istrm, received_message);
    }

    clearbuf(m_inbuf);

    current_chat.append(received_message + "\n");

    clear_console();
    m_console.write(current_chat);
    m_console.write("Write your message: ");

    asio::async_read_until(m_sock, m_inbuf, "\n",
        [this](error_code ec, std::size_t /*bytes_transferred*/) {
          message_wait_loop(ec);
        });
  }

  void onMessageSent(error_code ec)
  {
    logger->info("in onMessageSent ({})", ec.message());
    if (ec.failed()) {
      return;
    }
  }

private:
  using executor_type = asio::thread_pool::executor_type;
  asio::thread_pool m_ios { 2 }; // two io threads

  using work_guard = asio::executor_work_guard<executor_type>;
  work_guard m_work { m_ios.get_executor() };

  using strand = asio::strand<executor_type>;
  strand ui_strand { m_ios.get_executor() };
  strand io_strand { m_ios.get_executor() };

  tcp::socket m_sock { io_strand }; //!< The socket for the client application to connect to the server
  asio::streambuf m_inbuf;
  std::string m_outbuf;

  struct LogonState {
    SignChoice sign_choice;
    std::string login;
  } logonState;
  std::string current_chat;

  std::shared_ptr<spdlog::logger> logger;
  Console m_console;
};

int main()
{
  std::ifstream cfg_istrm("./cfg_client.json");
  json cfg = json::parse(cfg_istrm);

  std::string raw_ip = cfg["server_ip"];
  unsigned short port_num = cfg["server_port"];

  try {
    ChatSession client;
    client.connect(raw_ip, port_num);
  } catch (boost::system::system_error const& e) {
    std::cout << e.what() << " " << e.code().message();
  }
}
