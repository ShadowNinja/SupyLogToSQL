#ifndef IRCLOG_HEADER
#define IRCLOG_HEADER

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <sstream>

#include "sqlite3.h"


namespace IRCLog {

enum class MessageType : uint8_t {
	Privmsg,
	Notice,
	Action,
	Join,
	Part,
	Quit,
	Kick,
	Nick,
	Mode,
	Topic,
};


struct Network {
	uint8_t id;
	std::string name;
};


struct Buffer {
	uint16_t id;
	std::string name;
	uint8_t networkid;

	//Network * network;
};


struct Sender {
	uint32_t id;
	std::string nick;
	std::string user;
	std::string host;
};


struct Message {
	uint64_t id;
	time_t time;
	MessageType type;
	uint16_t bufferid;
	uint32_t senderid;
	std::string text;

	//Buffer * buffer;
	//Sender * sender;
};


class DB {
private:
	sqlite3 * dbh;

	sqlite3_stmt * stmt_add_message;
	sqlite3_stmt * stmt_add_sender;
	sqlite3_stmt * stmt_add_buffer;
	sqlite3_stmt * stmt_add_network;
	sqlite3_stmt * stmt_begin;
	sqlite3_stmt * stmt_commit;

	std::vector<Buffer> buffers;
	std::vector<Network> networks;
	std::vector<Sender> senders;

	bool inTransaction;

	void loadBuffers();
	void loadNetworks();
	void loadSenders();

public:
	DB(const std::string & filename);
	DB(const DB & db) = delete;
	~DB();

	void addMessage(Message & msg);
	Buffer * addBuffer(Buffer & msg);
	Network * addNetwork(Network & msg);
	Sender * addSender(Sender & msg);

	Buffer * getBuffer(const std::string & netName, const std::string & name, const bool create = true);
	Network * getNetwork(const std::string & name, const bool create = true);
	Network * getNetwork(const uint8_t id);
	Network * getNetwork(const Buffer & buf);
	Sender * getSender(Sender & snd, const bool create = true);

	const Sender * guessSenderByNick(const std::string & nick) const;

	void beginSave();
	void endSave();
};


class SQLError : public std::runtime_error {
	using runtime_error::runtime_error;
};

};  // namespace IRCLog

#endif

