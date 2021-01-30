#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <cstring>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

/// reads a character from console without echo.
int getChar()
{
    struct termios oldattr;
    tcgetattr(STDIN_FILENO, &oldattr);
    struct termios newattr = oldattr;
    newattr.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newattr);
    const int ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);
    return ch;
}

class Console
{
  private:
    // mutex for console I/O
    std::mutex _mtx;
    // current input
    std::string _input;
    // prompt output
    std::string _prompt;

  public:
    Console() {}

    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    std::string read();

    void write(const char *text, size_t size);
    void write(const char *text) { write(text, strlen(text)); }
    void write(const std::string &text) { write(text.c_str(), text.size()); }
};

std::string Console::read()
{
    { // activate prompt
        std::lock_guard<std::mutex> lock(_mtx);
        _prompt = "> ";
        _input.clear();
        std::cout << _prompt << std::flush;
    }
    enum
    {
        Enter = '\n',
        BackSpc = 127
    };
    for (;;)
    {
        switch (int c = getChar())
        {
        case Enter:
        {
            std::lock_guard<std::mutex> lock(_mtx);
            std::string input = _input;
            _prompt.clear();
            _input.clear();
            std::cout << std::endl;
            return input;
        } // unreachable: break;
        case BackSpc:
        {
            std::lock_guard<std::mutex> lock(_mtx);
            if (_input.empty())
                break;
            _input.pop_back();
            std::cout << "\b \b" << std::flush;
        }
        break;
        default:
        {
            if (c < ' ' || c >= '\x7f')
                break;
            std::lock_guard<std::mutex> lock(_mtx);
            _input += c;
            std::cout << (char)c << std::flush;
        }
        break;
        }
    }
}

void Console::write(const char *text, size_t len)
{
    if (!len)
        return;
    bool eol = text[len - 1] == '\n';
    std::lock_guard<std::mutex> lock(_mtx);
    // remove current input echo
    if (size_t size = _prompt.size() + _input.size())
    {
        std::cout
            << std::setfill('\b') << std::setw(size) << ""
            << std::setfill(' ') << std::setw(size) << ""
            << std::setfill('\b') << std::setw(size) << "";
    }
    // print text
    std::cout << text;
    if (!eol)
        std::cout << std::endl;
    // print current input echo
    std::cout << _prompt << _input << std::flush;
}

struct Flags //this flag is shared between both the threads
{
    // flag: true then exit communication thread and main loop
    bool exit;
    // flag: true then start data processing
    bool start;
    // the mini console
    Console console;

    // constructor.
    Flags() : exit(false), start(true) {}
};

void dataProc(Flags &shared)
{
    int i = 0;
    while (!shared.exit)
    {
        while (!shared.start)
        {
            if (shared.exit)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        shared.console.write("Starting data processing.");
        for (;;)
        {
            if (!shared.start || shared.exit)
            {

                shared.console.write("Data processing stopped.");
                while (!shared.start || shared.exit)
                {
                    if (shared.exit)
                        return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                shared.console.write("Data processing restarted.");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            {
                std::ostringstream fmt;
                fmt << "Cake " << ++i;
                shared.console.write(fmt.str());
            }
        }
        shared.console.write("Data processing done.");
        shared.start = false;
    }
}

void processInput(const std::string &input, Flags &shared)
{

    if (strcasecmp(input.c_str(),"start")==0)
        shared.start = true;
    else if (strcasecmp(input.c_str(),"stop")==0)
        shared.start = false;
    else if (strcasecmp(input.c_str(),"exit")==0)
        shared.exit = true;
    else if (input.size())
        shared.console.write("Wrong command!");
}

