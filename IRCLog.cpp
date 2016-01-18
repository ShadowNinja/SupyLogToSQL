#include "IRCLog.hpp"

#define SQLRES(f, good, msg) do { \
		res = (sqlite3_##f);\
		if (res != good) { \
			throw SQLError(std::string("SQLite3 error " msg ": ") + \
					sqlite3_errmsg(dbh)); \
		} \
	} while (0);

#define SQLOK(f, msg) SQLRES(f, SQLITE_OK, msg)
#define PREPARE_STMT(var, name, stmt) \
		SQLOK(prepare_v2(dbh, stmt, -1, &(var), NULL), \
			"preparing " name " statement");
#define FINALIZE_STMT(var, name) \
		SQLOK(finalize(var), "finalizing " name " statement");

namespace IRCLog
{

DB::DB(const std::string & filename)
{
	int res;
	SQLOK(open(filename.c_str(), &dbh), "opening database");

	SQLOK(exec(dbh,
			"BEGIN;\n"
			"CREATE TABLE IF NOT EXISTS sender (\n"
			"	id INTEGER NOT NULL,\n"
			"	nick VARCHAR,\n"
			"	user VARCHAR,\n"
			"	host VARCHAR,\n"
			"	PRIMARY KEY (id)\n"
			");\n"
			"CREATE TABLE IF NOT EXISTS network (\n"
			"	id INTEGER NOT NULL,\n"
			"	name VARCHAR,\n"
			"	PRIMARY KEY (id)\n"
			");\n"
			"CREATE TABLE IF NOT EXISTS buffer (\n"
			"	id INTEGER NOT NULL,\n"
			"	networkid INTEGER NOT NULL,\n"
			"	name VARCHAR,\n"
			"	PRIMARY KEY (id),\n"
			"	FOREIGN KEY(networkid) REFERENCES network (id)\n"
			");\n"
			"CREATE TABLE IF NOT EXISTS log (\n"
			"	id INTEGER NOT NULL,\n"
			"	type INTEGER NOT NULL,\n"
			"	timestamp INTEGER NOT NULL,\n"
			"	bufferid INTEGER NOT NULL,\n"
			"	senderid INTEGER NOT NULL,\n"
			"	message VARCHAR,\n"
			"	PRIMARY KEY (id),\n"
			"	FOREIGN KEY(bufferid) REFERENCES buffer (id),\n"
			"	FOREIGN KEY(senderid) REFERENCES sender (id)\n"
			");\n"
			"CREATE INDEX IF NOT EXISTS logTimestamp ON log(timestamp);\n"
			"COMMIT;\n",
		NULL, NULL, NULL), "initializing database");

	PREPARE_STMT(stmt_add_message, "message insertion",
		"INSERT INTO log"
			" (timestamp, type, bufferid, senderid, message)"
		" VALUES (?, ?, ?, ?, ?)");

	PREPARE_STMT(stmt_add_buffer, "buffer insertion",
		"INSERT INTO buffer (networkid, name) "
		"VALUES (?, ?)");

	PREPARE_STMT(stmt_add_network, "network insertion",
		"INSERT INTO network (name) "
		"VALUES (?)");

	PREPARE_STMT(stmt_add_sender, "sender insertion",
		"INSERT INTO sender (nick, user, host) "
		"VALUES (?, ?, ?)");

	PREPARE_STMT(stmt_begin, "begin", "BEGIN");
	PREPARE_STMT(stmt_commit, "commit", "COMMIT");

	loadNetworks();
	loadBuffers();
	loadSenders();
}


DB::~DB()
{
	int res;
	FINALIZE_STMT(stmt_add_message, "message insertion");
	FINALIZE_STMT(stmt_add_buffer, "buffer insertion");
	FINALIZE_STMT(stmt_add_network, "network insertion");
	FINALIZE_STMT(stmt_add_sender, "sender insertion");
	FINALIZE_STMT(stmt_begin, "begin");
	FINALIZE_STMT(stmt_commit, "commit");

	SQLOK(close(dbh), "closing database");
}


void DB::loadBuffers()
{
	int res;
	sqlite3_stmt * stmt;
	PREPARE_STMT(stmt, "buffer loading",
		"SELECT * FROM buffer ORDER BY id ASC");

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Buffer buf;
		buf.id = sqlite3_column_int(stmt, 0);
		buf.networkid = sqlite3_column_int(stmt, 1);
		buf.name = (const char *) sqlite3_column_text(stmt, 2);

		buffers.push_back(std::move(buf));
	}
	FINALIZE_STMT(stmt, "buffer loading");
}


void DB::loadNetworks()
{
	int res;
	sqlite3_stmt * stmt;
	PREPARE_STMT(stmt, "network loading",
		"SELECT * FROM network ORDER BY id ASC");

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Network net;
		net.id = sqlite3_column_int(stmt, 0);
		net.name = (const char *) sqlite3_column_text(stmt, 1);

		networks.push_back(std::move(net));
	}
	FINALIZE_STMT(stmt, "network loading");
}


void DB::loadSenders()
{
	int res;
	sqlite3_stmt * stmt;
	PREPARE_STMT(stmt, "sender loading",
		"SELECT * FROM sender ORDER BY id ASC");

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Sender snd;
		snd.id = sqlite3_column_int(stmt, 0);
		snd.nick = (const char *) sqlite3_column_text(stmt, 1);
		snd.user = (const char *) sqlite3_column_text(stmt, 2);
		snd.host = (const char *) sqlite3_column_text(stmt, 3);

		senders.push_back(std::move(snd));
	}
	FINALIZE_STMT(stmt, "sender loading");
}


void DB::addMessage(Message &msg)
{
	int res;
	SQLOK(bind_int64(stmt_add_message, 1, msg.time),
			"binding message time");
	SQLOK(bind_int  (stmt_add_message, 2, (int) msg.type),
			"binding message type");
	SQLOK(bind_int  (stmt_add_message, 3, msg.bufferid),
			"binding message bufferid");
	SQLOK(bind_int  (stmt_add_message, 4, msg.senderid),
			"binding message senderid");
	SQLOK(bind_text (stmt_add_message, 5, msg.text.data(), msg.text.size(),
			NULL), "binding message text");

	SQLRES(step(stmt_add_message), SQLITE_DONE,
			"running message insertion statement");
	SQLOK(reset(stmt_add_message),
			"reseting message insertion statement");
}


Buffer * DB::addBuffer(Buffer &buf)
{
	int res;
	SQLOK(bind_int (stmt_add_buffer, 1, buf.networkid),
			"binding buffer networkid");
	SQLOK(bind_text(stmt_add_buffer, 2, buf.name.data(), buf.name.size(),
			NULL), "binding buffer name");

	SQLRES(step(stmt_add_buffer), SQLITE_DONE,
			"running buffer insertion statement");
	SQLOK(reset(stmt_add_buffer),
			"reseting buffer insertion statement");

	buf.id = sqlite3_last_insert_rowid(dbh);

	buffers.push_back(buf);
	return &buffers[buffers.size() - 1];
}


Network * DB::addNetwork(Network &net)
{
	int res;
	SQLOK(bind_text(stmt_add_network, 1, net.name.data(), net.name.size(),
			NULL), "binding network name");

	SQLRES(step(stmt_add_network), SQLITE_DONE,
			"running network insertion statement");
	SQLOK(reset(stmt_add_network),
			"reseting network insertion statement");

	net.id = sqlite3_last_insert_rowid(dbh);

	networks.push_back(net);
	return &networks[networks.size() - 1];
}


Sender * DB::addSender(Sender &snd)
{
	int res;
	SQLOK(bind_text(stmt_add_sender, 1, snd.nick.data(), snd.nick.size(),
			NULL), "binding sender nick");
	SQLOK(bind_text(stmt_add_sender, 2, snd.user.data(), snd.user.size(),
			NULL), "binding sender ident");
	SQLOK(bind_text(stmt_add_sender, 3, snd.host.data(), snd.host.size(),
			NULL), "binding sender host");

	SQLRES(step(stmt_add_sender), SQLITE_DONE,
			"running sender insertion statement");
	SQLOK(reset(stmt_add_sender),
			"reseting sender insertion statement");

	snd.id = sqlite3_last_insert_rowid(dbh);

	senders.push_back(snd);
	return &senders[senders.size() - 1];
}


Buffer * DB::getBuffer(const std::string & netName, const std::string & name, const bool create)
{
	for (Buffer & buf : buffers) {
		if (buf.name == name && getNetwork(buf.networkid)->name == netName) {
			return &buf;
		}
	}
	if (create) {
		Buffer buf;
		buf.networkid = getNetwork(netName, true)->id;
		buf.name = name;
		return addBuffer(buf);
	}
	return NULL;
}


Network * DB::getNetwork(const std::string & name, const bool create)
{
	for (Network & net : networks) {
		if (net.name == name) {
			return &net;
		}
	}
	if (create) {
		Network net;
		net.name = name;
		return addNetwork(net);
	}
	return NULL;
}


Network * DB::getNetwork(const uint8_t id)
{
	for (Network & net : networks) {
		if (net.id == id) {
			return &net;
		}
	}
	return NULL;
}


Sender * DB::getSender(Sender & snd, const bool create)
{
	for (Sender & s : senders) {
		if (s.nick == snd.nick &&
		    s.user == snd.user &&
		    s.host == snd.host) {
			return &s;
		}
	}
	if (create) {
		return addSender(snd);
	}
	return NULL;
}


const Sender * DB::guessSenderByNick(const std::string & nick) const
{
	// Find the most recent sender that has the same nick
	for (auto it = senders.rbegin(); it != senders.rend(); it++) {
		if (it->nick == nick) {
			return &(*it);
		}
	}
	return NULL;
}


void DB::beginSave()
{
	int res;
	SQLRES(step(stmt_begin), SQLITE_DONE, "running begin statement");
	SQLOK(reset(stmt_begin), "reseting begin statement");
	inTransaction = true;
}


void DB::endSave()
{
	int res;
	SQLRES(step(stmt_commit), SQLITE_DONE, "running commit statement");
	SQLOK(reset(stmt_commit), "reseting commit statement");
	inTransaction = false;
}

};  // namespace IRCLog

