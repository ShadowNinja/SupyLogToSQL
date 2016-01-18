#include "IRCLog.hpp"

#define SQLRES(f, good) \
		res = (sqlite3_##f);\
		if (res != good) {\
			throw SQLError(sqlite3_errmsg(dbh), __FILE__, __LINE__);\
		}
#define SQLOK(f) SQLRES(f, SQLITE_OK)

namespace IRCLog
{

DB::DB(const std::string & filename)
{
	int res;
	SQLOK(open(filename.c_str(), &dbh));

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
		NULL, NULL, NULL));

	SQLOK(prepare_v2(dbh,
			"INSERT INTO log"
				" (timestamp, type, bufferid, senderid, message)"
			" VALUES (?, ?, ?, ?, ?);",
		-1, &stmt_add_message, NULL));

	SQLOK(prepare_v2(dbh,
			"INSERT INTO buffer"
				" (networkid, name)"
			" VALUES (?, ?);",
		-1, &stmt_add_buffer, NULL));

	SQLOK(prepare_v2(dbh,
			"INSERT INTO network"
				" (name)"
			" VALUES (?);",
		-1, &stmt_add_network, NULL));

	SQLOK(prepare_v2(dbh,
			"INSERT INTO sender "
				"(nick, user, host)"
			" VALUES (?, ?, ?);",
		-1, &stmt_add_sender, NULL));

	SQLOK(prepare_v2(dbh,
			"BEGIN",
		-1, &stmt_begin, NULL));

	SQLOK(prepare_v2(dbh,
			"COMMIT",
		-1, &stmt_commit, NULL));

	loadNetworks();
	loadBuffers();
	loadSenders();
}


DB::~DB()
{
	int res;
	SQLOK(finalize(stmt_add_message));
	SQLOK(finalize(stmt_add_buffer));
	SQLOK(finalize(stmt_add_network));
	SQLOK(finalize(stmt_add_sender));
	SQLOK(finalize(stmt_begin));
	SQLOK(finalize(stmt_commit));

	SQLOK(close(dbh));
}


void DB::loadBuffers()
{
	int res;
	sqlite3_stmt * stmt;
	SQLOK(prepare_v2(dbh,
			"SELECT * FROM buffer ORDER BY id ASC;",
		-1, &stmt, NULL));

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Buffer buf;
		buf.id = sqlite3_column_int(stmt, 0);
		buf.networkid = sqlite3_column_int(stmt, 1);
		buf.name = (const char *) sqlite3_column_text(stmt, 2);

		buffers.push_back(std::move(buf));
	}
	SQLOK(finalize(stmt));
}


void DB::loadNetworks()
{
	int res;
	sqlite3_stmt * stmt;
	SQLOK(prepare_v2(dbh,
			"SELECT * FROM network ORDER BY id ASC;",
		-1, &stmt, NULL));

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Network net;
		net.id = sqlite3_column_int(stmt, 0);
		net.name = (const char *) sqlite3_column_text(stmt, 1);

		networks.push_back(std::move(net));
	}
	SQLOK(finalize(stmt));
}


void DB::loadSenders()
{
	int res;
	sqlite3_stmt * stmt;
	SQLOK(prepare_v2(dbh,
			"SELECT * FROM sender ORDER BY id ASC;",
		-1, &stmt, NULL));

	while ((res = sqlite3_step(stmt)) == SQLITE_ROW) {
		Sender snd;
		snd.id = sqlite3_column_int(stmt, 0);
		snd.nick = (const char *) sqlite3_column_text(stmt, 1);
		snd.user = (const char *) sqlite3_column_text(stmt, 2);
		snd.host = (const char *) sqlite3_column_text(stmt, 3);

		senders.push_back(std::move(snd));
	}
	SQLOK(finalize(stmt));
}


void DB::addMessage(Message &msg)
{
	int res;
	SQLOK(bind_int64(stmt_add_message, 1, msg.time));
	SQLOK(bind_int  (stmt_add_message, 2, (int) msg.type));
	SQLOK(bind_int  (stmt_add_message, 3, msg.bufferid));
	SQLOK(bind_int  (stmt_add_message, 4, msg.senderid));
	SQLOK(bind_text (stmt_add_message, 5, msg.text.data(), msg.text.size(), NULL));

	SQLRES(step(stmt_add_message), SQLITE_DONE);
	SQLOK(reset(stmt_add_message));
}


Buffer * DB::addBuffer(Buffer &buf)
{
	int res;
	SQLOK(bind_int (stmt_add_buffer, 1, buf.networkid));
	SQLOK(bind_text(stmt_add_buffer, 2, buf.name.data(), buf.name.size(), NULL));

	SQLRES(step(stmt_add_buffer), SQLITE_DONE);
	SQLOK(reset(stmt_add_buffer));

	buf.id = sqlite3_last_insert_rowid(dbh);

	buffers.push_back(buf);
	return &buffers[buffers.size() - 1];
}


Network * DB::addNetwork(Network &net)
{
	int res;
	SQLOK(bind_text(stmt_add_network, 1, net.name.data(), net.name.size(), NULL));

	SQLRES(step(stmt_add_network), SQLITE_DONE);
	SQLOK(reset(stmt_add_network));

	net.id = sqlite3_last_insert_rowid(dbh);

	networks.push_back(net);
	return &networks[networks.size() - 1];
}


Sender * DB::addSender(Sender &snd)
{
	int res;
	SQLOK(bind_text(stmt_add_sender, 1, snd.nick.data(), snd.nick.size(), NULL));
	SQLOK(bind_text(stmt_add_sender, 2, snd.user.data(), snd.user.size(), NULL));
	SQLOK(bind_text(stmt_add_sender, 3, snd.host.data(), snd.host.size(), NULL));

	SQLRES(step(stmt_add_sender), SQLITE_DONE);
	SQLOK(reset(stmt_add_sender));

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
	SQLRES(step(stmt_begin), SQLITE_DONE);
	SQLOK(reset(stmt_begin));
	inTransaction = true;
}


void DB::endSave()
{
	int res;
	SQLRES(step(stmt_commit), SQLITE_DONE);
	SQLOK(reset(stmt_commit));
	inTransaction = false;
}

#undef SQLOK
#undef SQLRES

};  // namespace IRCLog

