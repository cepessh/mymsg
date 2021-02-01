#include <algorithm>
#include <boost/asio.hpp>
#include <boost/core/ignore_unused.hpp>
#include <cmath>
#include <memory>
#include <myconsole.h>
#include <mymsg_mysql_query.h>
#include <mymsg_tools.h>

#include <SQLAPI.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>

using json = nlohmann::json;
namespace asio = boost::asio;
using asio::ip::tcp;
using namespace std::literals;
using boost::system::error_code;

#define EXIT_ON_ERR(ec)                                                        \
  if (ec.failed()) {                                                           \
    if (ec != asio::error::eof) {                                              \
      spdlog::error("Error in {}, code: {}, message: {}", __FUNCTION__,        \
          ec.value(), ec.message());                                           \
    } else {                                                                   \
      spdlog::info("in {} ({})", __FUNCTION__, ec.message());                  \
    }                                                                          \
    return;                                                                    \
  } /*else*/                                                                   \
  {                                                                            \
    spdlog::debug("in {}", __FUNCTION__);                                      \
  }

static void clearbuf(asio::streambuf& buf, bool warn_discard = true)
{
  if (auto n = buf.size(); warn_discard && n > 0) {
    std::string s(buffers_begin(buf.data()), buffers_end(buf.data()));
    spdlog::warn("Discarding {} bytes of unused buffer: '{}'", n, s);
  }
  buf.consume(buf.size());
}

/// Class that provides the actual seesion in the client-seesion model
class Session : public std::enable_shared_from_this<Session> {
  public:
  using DeliverMessage = std::function<void(
      long recipient_id, std::string sender_login, std::string message)>;

  Session(tcp::socket&& sock, std::shared_ptr<SAConnection> con,
      const int session_id,
      DeliverMessage delivery)
    : m_sock(std::move(sock)) // NOTE the socket executor is a strand now
    , m_con(std::move(con))
    , m_session_id(session_id)
    , m_delivery(std::move(delivery))
  {
  }

  void StartHandling()
  {
    /// Initiates communication with the client

    asio::async_write(m_sock, asio::buffer("Sign in [1] or Sign up [2]: \n"sv),
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onSignUpRequestSent(ec, bytes_transferred);
        });
  }

  void send_to_chat(std::string new_message)
  {
    m_outbuf = std::move(new_message);
    asio::async_write(m_sock, asio::buffer(m_outbuf),
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onAnotherPartyMessageSent(ec, bytes_transferred);
        });
  }

  void send_to_chat(std::string sender_login, std::string new_message)
  {
    send_to_chat(_form_message_str(sender_login, new_message));
  }

  long get_client_login_id() const { return m_client_id; }

  private:
  void onSignUpRequestSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    EXIT_ON_ERR(ec);

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onSignUpResponseReceived(ec, bytes_transferred);
        });
  }

  void onSignUpResponseReceived(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Processes sign in/sign up client's choice. Then prompts the client to
    /// enter their login
    EXIT_ON_ERR(ec);

    {
      std::istream istrm(&m_inbuf);
      std::string temp;
      std::getline(istrm, temp);
      sign_choice = static_cast<SignChoice>(std::stoi(temp));
    }

    clearbuf(m_inbuf);

    asio::async_write(m_sock, asio::buffer("Enter login: \n"sv),
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onLoginRequestSent(ec, bytes_transferred);
        });
  }

  void onLoginRequestSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Initiates reading of client's login
    EXIT_ON_ERR(ec);

    spdlog::info("Login request sent");

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onLoginReceived(ec, bytes_transferred);
        });
  }

  void onLoginReceived(const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Processes client's login

    /// Sign in: client's login must be registered. It should be found in the
    /// database Sign up: client's login must be unique. It should not be found
    /// in the database
    EXIT_ON_ERR(ec);

    {
      std::istream istrm(&m_inbuf);
      std::getline(istrm, m_login);
    }
    clearbuf(m_inbuf);

    spdlog::info("Login received: {}", m_login);

    bool login_is_valid = true;
    MySQLQueryState login_in_database = DBquery::is_login_found(m_con, m_login);

    // Shutdown session instance if there is error with the mysql query or mysql
    // server
    if (login_in_database == MYSQL_ERROR) {
      return;
    }

    // Corresponding server's response to client's sign choice
    switch (sign_choice) {
    case SIGN_IN: {
      if (login_in_database == NOT_FOUND) {
        login_is_valid = false;
        m_outbuf = std::to_string(NOT_REGISTERED) + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this, self = shared_from_this()](
                const error_code& ec, std::size_t bytes_transferred) {
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
            [this, self = shared_from_this()](
                const error_code& ec, std::size_t bytes_transferred) {
              onLoginRequestSent(ec, bytes_transferred);
            });
      }
      break;
    }
    }

    // When login is valid the server sends a password request
    if (login_is_valid) {
      m_outbuf = std::to_string(IS_VALID) + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this, self = shared_from_this()](
              const error_code& ec, std::size_t bytes_transferred) {
            onPasswordRequestSent(ec, bytes_transferred);
          });
    }
  }

  void onPasswordRequestSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Initiates read of client's password
    EXIT_ON_ERR(ec);

    spdlog::info("Password request sent");

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onPasswordReceived(ec, bytes_transferred);
        });
  }

  void onPasswordReceived(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Processes client's password

    /// Sign in. The login/password combination must match one in the database.
    /// Otherwise the client is asked to retry logging in.
    /// Sign up. The databases is updated with the new login/password
    /// combination.
    EXIT_ON_ERR(ec);

    {
      std::istream istrm(&m_inbuf);
      std::getline(istrm, m_password);
    }
    clearbuf(m_inbuf);

    spdlog::info("Password received: '{}'", m_password);
    bool credentials_registered
        = DBquery::are_credentials_registred(m_con, m_login, m_password);

    switch (sign_choice) {
    case SIGN_IN: {
      if (credentials_registered) {
        m_outbuf = std::to_string(AUTHORIZED) + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this, self = shared_from_this()](
                const error_code& ec, std::size_t bytes_transferred) {
              onAccountLogin(ec, bytes_transferred);
            });
      } else {
        m_outbuf = std::to_string(WRONG_LOG_PASS) + "\n";
        asio::async_write(m_sock, asio::buffer(m_outbuf),
            [this, self = shared_from_this()](
                const error_code& ec, std::size_t bytes_transferred) {
              onLoginRequestSent(ec, bytes_transferred);
            });
      }
      break;
    }
    case SIGN_UP: {
      // Updating database
      DBquery::register_user(m_con, m_login, m_password);

      m_outbuf = std::to_string(AUTHORIZED) + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this, self = shared_from_this()](
              const error_code& ec, std::size_t bytes_transferred) {
            onAccountLogin(ec, bytes_transferred);
          });
      break;
    }
    }
  }

  std::string get_chat_with(
      const std::string& login_initiator, const std::string& login_another)
  {
    /// Displays chat with another party

    /// Creates chat string, sets unread messages delimeter, updates the
    /// database marking corresponding messages as read

    spdlog::info("[{}] in get_chat_with", m_session_id);

    std::string res;
    m_client_id = DBquery::get_user_id(
        m_con, login_initiator); // The client's id in the database
    m_another_party_id
        = DBquery::get_user_id(m_con, login_another); // The id of another party

    m_id_to_log[m_client_id] = login_initiator;
    m_id_to_log[m_another_party_id] = login_another;

    try {
      // Selecting all messages between the parties sorted by date
      SACommand select_messages(m_con.get(),
          "select login_sender_id, login_recipient_id, date, content, "
          "read_by_recipient "
          "from Message where (login_sender_id = :1 and login_recipient_id = "
          ":2) or "
          "(login_sender_id = :2 and login_recipient_id = :1) order by date");
      select_messages << m_client_id << m_another_party_id;
      select_messages.Execute();

      bool unread_border_passed = false;
      // Composing chat string
      while (select_messages.FetchNext()) {
        if (!select_messages.Field("read_by_recipient").asBool()
            and !unread_border_passed
            and select_messages.Field("login_sender_id").asLong()
                != m_client_id) {
          res.append(UNREAD_BORDER + "\n");
          unread_border_passed = true;
        }
        res.append(_form_message_str(
            select_messages.Field("date").asString().GetMultiByteChars(),
            m_id_to_log[select_messages.Field("login_sender_id").asLong()],
            select_messages.Field("content").asString().GetMultiByteChars()));
      }

      // Selecting dialog id
      SACommand select_dialog_id(m_con.get(),
          "select dialog_id "
          "from Dialog where (login1_id = :1 and login2_id = :2) or "
          "(login1_id = :2 and login2_id = :1)");
      select_dialog_id << m_client_id << m_another_party_id;
      select_dialog_id.Execute();
      select_dialog_id.FetchNext();
      m_dialog_id = select_dialog_id.Field("dialog_id").asLong();
      select_dialog_id.Close();

      // Marking recently received messages as read
      SACommand mark_as_read(m_con.get(),
          "update Message set read_by_recipient = TRUE "
          "where (login_sender_id = :2 and login_recipient_id = :1)");

      mark_as_read << m_client_id << m_another_party_id;
      mark_as_read.Execute();
    } catch (SAException& x) {
      try {
        m_con->Rollback();
      } catch (SAException&) {
      }
      spdlog::error("Error in get_chat_with: "
          + std::string(x.ErrText().GetMultiByteChars()));
    }
    return res;
  }

  void onAccountLogin(const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Sends possible action choices
    EXIT_ON_ERR(ec);

    spdlog::info("'{}' has logged in", m_login);

    asio::async_write(m_sock,
        asio::buffer("List dialogs [1] or Add contact [2]: \n"sv),
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onActionRequestSent(ec, bytes_transferred);
        });
  }

  void onActionRequestSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Reads action choice
    EXIT_ON_ERR(ec);

    spdlog::info("Action request sent");

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onActionResponseReceived(ec, bytes_transferred);
        });
  }

  void onActionResponseReceived(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Processes action choice
    EXIT_ON_ERR(ec);

    {
      std::istream istrm(&m_inbuf);
      std::string temp;
      std::getline(istrm, temp);
      action_choice = static_cast<ActionChoice>(std::stoi(temp));
    }

    clearbuf(m_inbuf);

    spdlog::info("Action response received: {}", action_choice);

    switch (action_choice) {
    case LIST_DIALOGS: {
      // Sends dialog list

      m_outbuf = DBquery::get_dialogs_list(m_con, m_login) + "\n";
      asio::async_write(m_sock, asio::buffer(m_outbuf),
          [this, self = shared_from_this()](
              const error_code& ec, std::size_t bytes_transferred) {
            onDialogsListSent(ec, bytes_transferred);
          });
      break;
    }
    case ADD_CONTACT: {
      break;
    }
    }
  }

  void onDialogsListSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Reads user number in the dialog list
    EXIT_ON_ERR(ec);

    spdlog::info("[{}] in onDialogsListSent", m_session_id);

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onDialogUserLoginReceived(ec, bytes_transferred);
        });
  }

  void onDialogUserLoginReceived(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Sends the chat with the chosen user
    EXIT_ON_ERR(ec);

    spdlog::info("[{}] in onDialogUserLoginReceived", m_session_id);

    {
      std::istream istrm(&m_inbuf);
      std::getline(istrm, m_login_another);
    }
    clearbuf(m_inbuf);

    // Composing chat and recording chat status
    m_outbuf = get_chat_with(m_login, m_login_another) + "\n\n";

    asio::async_write(m_sock, asio::buffer(m_outbuf),
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onChatSent(ec, bytes_transferred);
        });
  }

  void onChatSent(const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    /// Reads new message from the client and sends messages from another party
    EXIT_ON_ERR(ec);

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onReceivedReady(ec, bytes_transferred);
        });
  }

  void onReceivedReady(const error_code& ec, std::size_t bytes_transferred);

  void receive_message()
  {
    spdlog::info("[{}] in receive_message", m_session_id);

    asio::async_read_until(m_sock, m_inbuf, '\n',
        [this, self = shared_from_this()](
            const error_code& ec, std::size_t bytes_transferred) {
          onMessageReceived(ec, bytes_transferred);
        });
  }

  void onMessageReceived(const error_code& ec, std::size_t bytes_transferred);

  void onAnotherPartyMessageSent(
      const error_code& ec, std::size_t /*bytes_transferred*/)
  {
    EXIT_ON_ERR(ec);

    spdlog::info("[{}] in onAnotherPartyMessageSent", m_session_id);
  }

  private:
  tcp::socket m_sock; ///< active socket that is used to communicate
                      ///< with the client
  std::shared_ptr<SAConnection> m_con; ///< Pointer to SAConnection object that
                                       ///< connects to the MySQL database
  int m_session_id;
  DeliverMessage m_delivery;
  std::string m_login;
  std::string m_password = "";
  std::string m_login_another;

  SignChoice sign_choice;
  ActionChoice action_choice;
  long m_dialog_id = -1, m_client_id = -1, m_another_party_id = -1;

  std::string m_outbuf;
  asio::streambuf m_inbuf;

  // Id to login translation tool map
  std::map<long, std::string> m_id_to_log;
};

/// Class that accepts connection to the server
class Acceptor {
  public:
  using Callback = std::function<void(tcp::socket&&)>;
  Acceptor(asio::any_io_executor ex, unsigned short port_num,
      std::shared_ptr<SAConnection> con, Callback callback)
    : m_con(std::move(con))
    , m_callback(std::move(callback))
    , m_acceptor(ex, tcp::endpoint(asio::ip::address_v4::any(), port_num))
    , m_isStopped(false)
  {
  }

  void Start()
  {
    m_acceptor.listen();
    InitAccept();
  }

  void Stop() { m_isStopped.store(true); }

  private:
  void InitAccept()
  {
    /// Initiates low-level async_accept. Creates an active socket to
    /// communicate with the client

    auto sock
        = std::make_shared<tcp::socket>(make_strand(m_acceptor.get_executor()));
    m_acceptor.async_accept(
        *sock, [this, sock](const error_code& ec) { onAccept(ec, sock); });
  }

  void onAccept(const error_code& ec, std::shared_ptr<tcp::socket> sock);

  private:
  std::shared_ptr<SAConnection>
      m_con; ///< Pointer to a database connection object
  Callback m_callback;
  tcp::acceptor m_acceptor; ///< low-level acceptor object
  std::atomic<bool>
      m_isStopped; ///< atomic variable used to stop acceptor between threads
};

/// Class that initiates connection with the database and accepting connections

class Server {
  public:
  Server(std::string cfg_path, unsigned thread_pool_size)
    : m_ios(thread_pool_size)
  {
    assert(thread_pool_size > 0);
    /// Initiates connection with the database

    std::ifstream cfg_istrm(cfg_path);
    json cfg = json::parse(cfg_istrm);

    std::string database_name = cfg["database_name"];
    std::string server_name = cfg["server_name"];
    std::string database_user = cfg["database_user"];
    std::string password = cfg["password"];

    std::string connection_string = server_name + "@" + database_name;

    m_con.reset(new SAConnection);
    m_con->Connect(_TSA(connection_string.c_str()), _TSA(database_user.c_str()),
        _TSA(password.c_str()), SA_MySQL_Client);
    spdlog::info("Connected to database {}", connection_string);
  }

  void Start(unsigned short port_num)
  {
    /// Creates and starts Acceptor instance, spawns threads with initiated
    /// asio::io_context::run
    auto async_callback = [this](tcp::socket&& socket) {
      post(m_strand, [this, s = std::move(socket)]() mutable {
        _new_connection(std::move(s));
      });
    };
    m_acc
        = std::make_unique<Acceptor>(m_strand, port_num, m_con, async_callback);
    m_acc->Start();
  }

  void Stop()
  {
    /// Halts Acceptor instance and waits till event loops in spawned threads
    /// end
    m_work.reset();
    m_acc->Stop();
    m_ios.stop(); // TODO instead shutdown m_sessions
    m_ios.join();
  }

  private:
  using executor = asio::thread_pool::executor_type;
  using work_guard = asio::executor_work_guard<executor>;
  using strand = asio::strand<executor>;
  asio::thread_pool m_ios;
  work_guard m_work { m_ios.get_executor() };
  strand m_strand { make_strand(m_ios.get_executor()) };
  std::unique_ptr<Acceptor> m_acc;
  std::shared_ptr<SAConnection> m_con;

  // now private, synchronization managed by strand
  std::map<int, std::weak_ptr<Session>> m_sessions;

  void _deliver_message(
      long recipient_id, std::string sender_login, std::string msg)
  {
    assert(m_strand.running_in_this_thread());

    auto it = std::find_if(
        begin(m_sessions), end(m_sessions), [recipient_id](auto& entry) {
          auto& [session_id, weak] = entry;
          if (auto sess = weak.lock()) {
            return sess->get_client_login_id() == recipient_id;
          }
          return false;
        });

    if (it != end(m_sessions)) {
      if (auto sess = it->second.lock()) {
        spdlog::info("[{}] sends to live recipient_id {}: '{}'", sender_login,
            recipient_id, msg);
        sess->send_to_chat(sender_login, msg);
      }
    }
  }

  void _new_connection(tcp::socket&& socket)
  {
    // garbage collect deceased sessions:
#if __cpp_lib_erase_if // requires c++20
    std::erase_if(m_sessions, [](auto& p) { return p.second.expired(); });
#else
    // do it manually, but I'm too lazy right now
#endif
    int session_id = m_sessions.empty() ? 1 : m_sessions.rbegin()->first + 1;

    auto async_deliver = [this](long recipient_id, std::string sender_login,
                             std::string msg) mutable {
      post(m_strand,
          [this, r = recipient_id, sender_login,
              msg = std::move(msg)]() mutable {
            _deliver_message(r, std::move(sender_login), std::move(msg));
          });
    };

    auto session = std::make_shared<Session>(
        std::move(socket), m_con, session_id, async_deliver);

    bool ok = m_sessions.emplace(session_id, session).second;
    assert(ok); // session_id should be unique
    boost::ignore_unused(ok);

    session->StartHandling();
  }
};

const unsigned int DEFAULT_THREAD_POOL_SIZE = 2;

void Acceptor::onAccept(const error_code& ec, std::shared_ptr<tcp::socket> sock)
{
  /// Logs accept status and closes m_acceptor if there is a signal to stop

  if (!ec.failed()) {
    spdlog::info("Accepted connection from IP: {}",
        sock->remote_endpoint().address().to_string());
    m_callback(std::move(*sock));
    sock.reset();
  } else {
    spdlog::error(
        "Error in onAccept, code: {}, message: {}", ec.value(), ec.message());
  }

  if (!m_isStopped.load()) {
    InitAccept();
  } else {
    m_acceptor.close();
  }
}

void Session::onReceivedReady(
    const error_code& ec, std::size_t /*bytes_transferred*/)
{
  EXIT_ON_ERR(ec);

  spdlog::info("[{}] in onReceivedReady", m_session_id);

  Status status = Status::WRONG_LOG_PASS;
  {
    std::istream istrm(&m_inbuf);
    std::string temp;
    std::getline(istrm, temp);

    status = static_cast<Status>(std::stoi(temp));
  }
  clearbuf(m_inbuf);

  if (status == READY_TO_CHAT) {
    receive_message();
  }
}

void Session::onMessageReceived(
    const error_code& ec, std::size_t /*bytes_transferred*/)
{
  /// Updates the database with the new message
  EXIT_ON_ERR(ec);

  std::string new_message;
  {
    std::istream istrm(&m_inbuf);
    std::getline(istrm, new_message);
  }
  clearbuf(m_inbuf);

  spdlog::info("[{}] received message '{}'", m_session_id, new_message);

  m_delivery(m_another_party_id, m_login, new_message);

  DBquery::insert_message(
      m_con, m_client_id, m_another_party_id, new_message, m_dialog_id);
  receive_message();
}

int main()
{
  unsigned short port_num = 3333;

  spdlog::set_pattern("[%d/%m/%Y] [%H:%M:%S:%f] [%n] %^[%l]%$ %v");
  try {
    unsigned int thread_pool_size = std::thread::hardware_concurrency() * 2;
    if (thread_pool_size == 0) {
      thread_pool_size = DEFAULT_THREAD_POOL_SIZE;
    }

    Server srv("./cfg_server.json", thread_pool_size);

    srv.Start(port_num);
    std::this_thread::sleep_for(500s);
    srv.Stop();
  } catch (boost::system::system_error& e) {
    std::cout << e.what() << " " << e.code().message();
  }
}
