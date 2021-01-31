#include<boost/asio.hpp>
#include <memory>
#include <boost/system/detail/error_code.hpp>
#include<myconsole.h>
#include<mymsg_mysql_query.h>
#include<mymsg_tools.h>

#include<nlohmann/json.hpp>
#include<SQLAPI.h>
#include<spdlog/spdlog.h>

#include<atomic>
#include<ctime>
#include<fstream>
#include<iomanip>
#include<iostream>
#include<memory>
#include<sstream>
#include<thread>
#include <utility>


using json = nlohmann::json;
namespace asio = boost::asio;
using namespace std::literals;
using boost::system::error_code;

static void clearbuf(asio::streambuf& buf, bool warn_discard = true) {
  if (auto n = buf.size(); warn_discard && n > 0) {
    std::string s(buffers_begin(buf.data()), buffers_end(buf.data()));
    spdlog::warn("Discarding {} bytes of unused buffer: '{}'", n, s);
  }
  buf.consume(buf.size());
}

/// Struct to track active sessions of clients
struct Tracker {
  static std::mutex current_sessions_guard; ///< mutex to lock the map of current sessions between threads
  static std::map<long, long> current_sessions; 
  static std::map<long, int> client_to_service_id;
}; 

std::mutex Tracker::current_sessions_guard;
std::map<long, long> Tracker::current_sessions;
std::map<long, int> Tracker::client_to_service_id;


/// Class that provides the actual service in the client-service model
class Service {
public:
  Service(asio::ip::tcp::socket&& sock, std::shared_ptr<SAConnection> con, const int service_id):
    m_sock(std::move(sock)),
    m_con(std::move(con)),
    service_id(service_id) 
  { }

  void StartHandling() {
    /// Initiates communication with the client

    asio::async_write(m_sock, asio::buffer("Sign in [1] or Sign up [2]: \n"sv),
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onSignUpRequestSent(ec, bytes_transferred); 
      });
  }
  
  asio::ip::tcp::socket& get_socket() {
    return m_sock;
  } 

  void send_to_chat(std::string new_message) {
    m_outbuf = std::move(new_message);
    asio::async_write(m_sock, asio::buffer(m_outbuf),
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onAnotherPartyMessageSent(ec, bytes_transferred); 
      });
  } 

private:
  void onSignUpRequestSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpRequestSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 
    asio::async_read_until(m_sock, m_sign_choice, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onSignUpResponseReceived(ec, bytes_transferred);
      });
  }

  void onSignUpResponseReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Processes sign in/sign up client's choice. Then prompts the client to enter their login

    if (ec.value() != 0) {
      spdlog::error("Error in onSignUpResponseReceived, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 
    
    std::istream istrm(&m_sign_choice);
    std::string temp;
    std::getline(istrm, temp);
    sign_choice = static_cast<SignChoice>(std::stoi(temp));

    clearbuf(m_sign_choice);

    asio::async_write(m_sock, asio::buffer("Enter login: \n"sv),
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onLoginRequestSent(ec, bytes_transferred); 
      });
  }

  void onLoginRequestSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Initiates reading of client's login

    if (ec.value() != 0) {
      spdlog::error("Error in onLoginRequestSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("Login request sent");

    asio::async_read_until(m_sock, m_login_buf, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onLoginReceived(ec, bytes_transferred);
      });
  } 

  void onLoginReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Processes client's login

    /// Sign in: client's login must be registered. It should be found in the database
    /// Sign up: client's login must be unique. It should not be found in the database

    if (ec.value() != 0) {
      spdlog::error("Error in onLoginReceived, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 

    std::istream istrm(&m_login_buf);
    std::getline(istrm, login);
    clearbuf(m_login_buf);

    spdlog::info("Login received: {}", login);

    bool login_is_valid = true;
    MySQLQueryState login_in_database = DBquery::is_login_found(m_con, login);
    
    // Shutdown Service instance if there is error with the mysql query or mysql server
    if (login_in_database == MYSQL_ERROR) {
      onFinish();
      return;
    } 

    // Corresponding server's response to client's sign choice
    switch (sign_choice) {
      case SIGN_IN: {
        if (login_in_database == NOT_FOUND) {
          login_is_valid = false;
          m_outbuf = std::to_string(NOT_REGISTERED) + "\n";
          asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this](const error_code& ec, std::size_t bytes_transferred) {
              onLoginRequestSent(ec, bytes_transferred); 
            });
        } 
        break;
      } 
      case SIGN_UP: {
        if (login_in_database == FOUND) {
          login_is_valid = false;
          m_outbuf = std::to_string(IS_TAKEN) + "\n";
          asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this](const error_code& ec, std::size_t bytes_transferred) {
              onLoginRequestSent(ec, bytes_transferred); 
            });
        }
        break;
      } 
    } 
    
    // When login is valid the server sends a password request
    if (login_is_valid) {
      m_outbuf = std::to_string(IS_VALID) +"\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
        [this](const error_code& ec, std::size_t bytes_transferred) {
          onPasswordRequestSent(ec, bytes_transferred); 
        });
    }
  } 

  void onPasswordRequestSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Initiates read of client's password

    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordRequestSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 
    spdlog::info("Password request sent");

    asio::async_read_until(m_sock, m_password_buf, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onPasswordReceived(ec, bytes_transferred);
      });
  }

  void onPasswordReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Processes client's password

    /// Sign in. The login/password combination must match one in the database. 
    /// Otherwise the client is asked to retry logging in.
    /// Sign up. The databases is updated with the new login/password combination.
    
    if (ec.value() != 0) {
      spdlog::error("Error in onPasswordReceived, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    } 

    std::istream istrm(&m_password_buf);
    std::getline(istrm, password);
    clearbuf(m_password_buf);

    spdlog::info("Password received: '{}'", password);
    bool credentials_registered = DBquery::are_credentials_registred(m_con, login, password);  

    switch (sign_choice) {
      case SIGN_IN: {
        if (credentials_registered) {
          m_outbuf = std::to_string(AUTHORIZED) + "\n";
          asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this](const error_code& ec, std::size_t bytes_transferred) {
              onAccountLogin(ec, bytes_transferred); 
            });
        } else {
          m_outbuf = std::to_string(WRONG_LOG_PASS) + "\n";
          asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this](const error_code& ec, std::size_t bytes_transferred) {
              onLoginRequestSent(ec, bytes_transferred); 
            });
        } 
        break;
      } 
      case SIGN_UP: {
        // Updating database
        DBquery::register_user(m_con, login, password);

        m_outbuf = std::to_string(AUTHORIZED) + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](const error_code& ec, std::size_t bytes_transferred) {
            onAccountLogin(ec, bytes_transferred); 
          });
        break;
      } 
    } 
  }

  std::string get_chat_with(const std::string& login_initiator, const std::string& login_another) {
    /// Displays chat with another party and records chat status in Tracker::current_session

    /// Creates chat string, sets unread messages delimeter, updates the database
    /// marking corresponding messages as read

    spdlog::info("[{}] in get_chat_with", service_id);

    std::string res;
    client_id = DBquery::get_user_id(m_con, login_initiator); // The client's id in the database
    another_party_id = DBquery::get_user_id(m_con, login_another); // The id of another party

    // Recording the client's current chat status
    std::unique_lock<std::mutex> session_map_lock(Tracker::current_sessions_guard); 
    Tracker::current_sessions[client_id] = another_party_id;
    Tracker::client_to_service_id[client_id] = service_id;
    session_map_lock.unlock();

    id_to_log[client_id] = login_initiator;
    id_to_log[another_party_id] = login_another;

    try {
      // Selecting all messages between the parties sorted by date
      SACommand select_messages(m_con.get(),
          "select login_sender_id, login_recipient_id, date, content, read_by_recipient "
          "from Message where (login_sender_id = :1 and login_recipient_id = :2) or " 
          "(login_sender_id = :2 and login_recipient_id = :1) order by date");
      select_messages << client_id << another_party_id;
      select_messages.Execute();

      bool unread_border_passed = false;
      // Composing chat string
      while (select_messages.FetchNext()) {
        if (!select_messages.Field("read_by_recipient").asBool()
            and !unread_border_passed
            and select_messages.Field("login_sender_id").asLong() != client_id) {
          res.append(UNREAD_BORDER + "\n");
          unread_border_passed = true;
        } 
        res.append(_form_message_str(select_messages.Field("date").asString().GetMultiByteChars(),
              id_to_log[select_messages.Field("login_sender_id").asLong()], 
              select_messages.Field("content").asString().GetMultiByteChars()));
      }

      // Selecting dialog id
      SACommand select_dialog_id(m_con.get(),
          "select dialog_id "
          "from Dialog where (login1_id = :1 and login2_id = :2) or " 
          "(login1_id = :2 and login2_id = :1)");
      select_dialog_id << client_id << another_party_id;
      select_dialog_id.Execute();
      select_dialog_id.FetchNext();
      dialog_id = select_dialog_id.Field("dialog_id").asLong();
      select_dialog_id.Close();

      // Marking recently received messages as read
      SACommand mark_as_read(m_con.get(),
        "update Message set read_by_recipient = TRUE "
        "where (login_sender_id = :2 and login_recipient_id = :1)");

      mark_as_read << client_id << another_party_id;
      mark_as_read.Execute();
    } catch (SAException &x) {
      try {
        m_con->Rollback();
      } catch (SAException &) {}
      spdlog::error("Error in get_chat_with: " + std::string(x.ErrText().GetMultiByteChars()));
    } 
    return res;
  } 

  void onAccountLogin(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Sends possible action choices

    if (ec.value() != 0) {
      spdlog::error("Error in onAccountLogin, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("'{}' has logged in", login);
     
    asio::async_write(m_sock, asio::buffer("List dialogs [1] or Add contact [2]: \n"sv),
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onActionRequestSent(ec, bytes_transferred); 
      });
  }
  
  void onActionRequestSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Reads action choice

    if (ec.value() != 0) {
      spdlog::error("Error in onActionRequestSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("Action request sent");

    asio::async_read_until(m_sock, m_action_choice, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onActionResponseReceived(ec, bytes_transferred);
      });
  } 

  void onActionResponseReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Processes action choice 

    if (ec.value() != 0) {
      spdlog::error("Error in onActionResponseReceived, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }

    std::istream istrm(&m_action_choice);
    std::string temp;
    std::getline(istrm, temp);
    action_choice = static_cast<ActionChoice>(std::stoi(temp));

    clearbuf(m_action_choice);

    spdlog::info("Action response received: {}", action_choice);

    switch (action_choice) {
      case LIST_DIALOGS: {
        // Sends dialog list

        m_outbuf = DBquery::get_dialogs_list(m_con, login) + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this](const error_code& ec, std::size_t bytes_transferred) {
            onDialogsListSent(ec, bytes_transferred); 
          });
        break;
      }  
      case ADD_CONTACT: {
        break;
      }  
    } 
  } 

  void onDialogsListSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Reads user number in the dialog list

    if (ec.value() != 0) {
      spdlog::error("Error in onDialogsListSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("[{}] in onDialogsListSent", service_id);

    asio::async_read_until(m_sock, m_dialog_user_login, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onDialogUserLoginReceived(ec, bytes_transferred);
      });
  }


  void onDialogUserLoginReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Sends the chat with the chosen user

    if (ec.value() != 0) {
      spdlog::error("Error in onDialogUserLoginReceived, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }
    spdlog::info("[{}] in onDialogUserLoginReceived", service_id);

    std::istream istrm(&m_dialog_user_login);
    std::getline(istrm, login_another);
    clearbuf(m_dialog_user_login);

    // Composing chat and recording chat status
    m_outbuf = get_chat_with(login, login_another) + "\n\n";

    asio::async_write(m_sock, asio::buffer(m_outbuf),
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onChatSent(ec, bytes_transferred); 
      });
  } 

  void onChatSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    /// Reads new message from the client and sends messages from another party

    if (ec.value() != 0) {
      spdlog::error("Error in onChatSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }
    
    asio::async_read_until(m_sock, m_ready_to_chat, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onReceivedReady(ec, bytes_transferred);
      });

  } 

  void onReceivedReady(const error_code& ec, std::size_t bytes_transferred);

  void receive_message() {
    spdlog::info("[{}] in receive_message", service_id);

    asio::async_read_until(m_sock, m_message, '\n',
      [this](const error_code& ec, std::size_t bytes_transferred) {
        onMessageReceived(ec, bytes_transferred);
      });
  } 
  
  void onMessageReceived(const error_code& ec, std::size_t bytes_transferred);

  void onAnotherPartyMessageSent(const error_code& ec, std::size_t /*bytes_transferred*/) {
    if (ec.value() != 0) {
      spdlog::error("Error in onAnotherPartyMessageSent, code: {}, message: {}", ec.value(), ec.message());
      onFinish();
      return;
    }

    spdlog::info("[{}] in onAnotherPartyMessageSent", service_id);
 } 

  void onFinish(); 

private:
  asio::ip::tcp::socket m_sock; ///< active socket that is used to communicate
                                ///< with the client
  std::shared_ptr<SAConnection> m_con; ///< Pointer to SAConnection object that
                                       ///< connects to the MySQL database
  int service_id;
  std::string m_response;
  std::string login;
  std::string password = "";
  std::string login_another;

  SignChoice sign_choice;
  ActionChoice action_choice;
  long dialog_id = -1, client_id = -1, another_party_id = -1;

  std::string m_outbuf;
  asio::streambuf m_request;
  asio::streambuf m_login_buf;
  asio::streambuf m_password_buf;
  asio::streambuf m_sign_choice;
  asio::streambuf m_action_choice;
  asio::streambuf m_dialog_user_login;
  asio::streambuf m_message;
  asio::streambuf m_another_party_message;
  asio::streambuf m_ready_to_chat;

  std::unique_ptr<std::thread> checker_th; 

  // Id to login translation tool map
  std::map<long, std::string> id_to_log;

};

/// Class that accepts connection to the server
class Acceptor {
public:
  Acceptor(asio::io_context& ios, unsigned short port_num, std::shared_ptr<SAConnection> con):
    m_ios(ios),
    m_con(std::move(con)),
    m_acceptor(m_ios, asio::ip::tcp::endpoint(asio::ip::address_v4::any(), port_num)),
    m_isStopped(false) {}

  void Start() {
    m_acceptor.listen();
    InitAccept();
  }

  void Stop() {
    m_isStopped.store(true);
  }

private:
  void InitAccept() {
    /// Initiates low-level async_accept. Creates an active socket to communicate with the client 

    std::shared_ptr<asio::ip::tcp::socket> sock(new asio::ip::tcp::socket(m_ios));
    m_acceptor.async_accept(*sock, [this, sock](const error_code& ec) {
          onAccept(ec, sock);
        });
  } 

  void onAccept(const error_code& ec, std::shared_ptr<asio::ip::tcp::socket> sock); 

private:
  asio::io_context& m_ios;
  std::shared_ptr<SAConnection> m_con;///< Pointer to a database connection object
  asio::ip::tcp::acceptor m_acceptor; ///< low-level asio::ip::tcp::acceptor object
  std::atomic<bool> m_isStopped;      ///< atomic variable used to stop acceptor between threads
}; 

/// Class that initiates connection with the database and accepting connections

class Server {
public:
  Server(std::string cfg_path) {
    /// Initiates connection with the database

    std::ifstream cfg_istrm(cfg_path);
    json cfg = json::parse(cfg_istrm);

    std::string database_name = cfg["database_name"];
    std::string server_name = cfg["server_name"];
    std::string database_user = cfg["database_user"];
    std::string password = cfg["password"];
    
    std::string connection_string = server_name + "@" + database_name;
   
    con.reset(new SAConnection); 
    con->Connect(
        _TSA(connection_string.c_str()),
        _TSA(database_user.c_str()),
        _TSA(password.c_str()),
        SA_MySQL_Client);
    spdlog::info("Connected to database {}", connection_string);
  }
  
  void Start(unsigned short port_num, unsigned int thread_pool_size) {
    /// Creates and starts Acceptor instance, spawns threads with initiated asio::io_context::run 

    assert(thread_pool_size > 0);
    acc = std::make_unique<Acceptor>(m_ios, port_num, con);
    acc->Start();

    for (size_t i = 0; i < thread_pool_size; i ++) {
      std::unique_ptr<std::thread> th(new std::thread(
            [this]() {
              m_ios.run();
            }));
      m_thread_pool.push_back(std::move(th));
    }   
  } 

  void Stop() {
    /// Halts Acceptor instance and waits till event loops in spawned threads end
    acc->Stop();
    m_ios.stop();

    for (auto& th: m_thread_pool) {
      th->join();
    } 
  } 
  static std::map<int, Service * > launched_services;
  
private:
  asio::io_context m_ios;
  std::unique_ptr<asio::io_context::work> m_work;
  std::unique_ptr<Acceptor> acc;
  std::vector<std::unique_ptr<std::thread>> m_thread_pool;
  std::shared_ptr<SAConnection> con;
};

std::map<int, Service *> Server::launched_services;

const unsigned int DEFAULT_THREAD_POOL_SIZE = 2;

void Acceptor::onAccept(const error_code& ec, std::shared_ptr<asio::ip::tcp::socket> sock) {
  /// Logs accept status and closes m_acceptor if there is a signal to stop

  if (ec.value() == 0) {
    spdlog::info("Accepted connection from IP: {}", sock->remote_endpoint().address().to_string());
    int service_id = Server::launched_services.empty()
      ? 1
      : Server::launched_services.rbegin()->first + 1;
    auto p = Server::launched_services[service_id] = new Service(std::move(*sock), m_con, service_id);
    p->StartHandling();
  } else {
    spdlog::error("Error in onAccept, code: {}, message: {}", ec.value(), ec.message());
  }  

  if (!m_isStopped.load()) {
    InitAccept();
  } else {
    m_acceptor.close();
  } 
} 

void Service::onFinish() {
  Server::launched_services.erase(service_id);
  delete this; // TODO use std::enable_shared_from_this
} 

void Service::onReceivedReady(const error_code& ec, std::size_t /*bytes_transferred*/) {
  if (ec.value() != 0) {
    spdlog::error("Error in onReceivedReady, code: {}, message: {}", ec.value(), ec.message());
    onFinish();
    return;
  }
  spdlog::info("[{}] in onReceivedReady", service_id);

  std::istream istrm(&m_ready_to_chat);
  std::string temp;
  std::getline(istrm, temp);

  auto status = static_cast<Status>(std::stoi(temp));
  clearbuf(m_ready_to_chat);

  if (status == READY_TO_CHAT) {
    receive_message();
  } else {
    onFinish();
    return;
  } 
} 

void Service::onMessageReceived(const error_code& ec, std::size_t /*bytes_transferred*/) {
  /// Updates the database with the new message

  if (ec.value() != 0) {
    spdlog::error("Error in onMessageReceived, code: {}, message: {}", ec.value(), ec.message());
    onFinish();
    return;
  }

  std::istream istrm(&m_message);
  std::string new_message;
  std::getline(istrm, new_message);
  clearbuf(m_message);

  spdlog::info("[{}] received message '{}'", service_id, new_message);

  std::unique_lock<std::mutex> tracker_lock(Tracker::current_sessions_guard);

  if (Tracker::current_sessions.find(another_party_id) != Tracker::current_sessions.end()) {
    if (Tracker::current_sessions[another_party_id] == client_id) {
      int another_party_service_id = Tracker::client_to_service_id[another_party_id];
      std::string formatted_msg = _form_message_str(login, new_message);
      
      spdlog::info("[{}] sends to chat '{}'", another_party_service_id, new_message);

      Server::launched_services[another_party_service_id]->send_to_chat(std::move(formatted_msg));
    }
  }
  tracker_lock.unlock();

  DBquery::insert_message(m_con, client_id, another_party_id, new_message, dialog_id);
  receive_message();
} 

int main() {
  unsigned short port_num = 3333;

  spdlog::set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v"); 
  try {
    Server srv("./cfg_server.json");
    
    unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;
    if (thread_pool_size == 0) {
      thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
    } 

    srv.Start(port_num, thread_pool_size);
    std::this_thread::sleep_for(500s);
    srv.Stop();

  } 
  catch (boost::system::system_error& e) {
    std::cout << e.what() << " " << e.code().message();
  } 
} 
