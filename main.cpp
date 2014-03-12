#include <cstdlib>
#include <ctime>
#include <cassert>
#include <cctype>
#include <string>
#include <fstream>
#include <iostream>

#include "IRCLog.hpp"

static_assert(sizeof(std::streampos) > 4, "std::streampos isn't big enough!");

using namespace IRCLog;

int main(int argc, char *argv[]);
bool readLine(std::istream & is, DB & db, Message & msg, const uint64_t line);
void genericMessage(const MessageType type, std::istream & is, DB & db,
		Message & msg, const char delim, const short skipNum);
time_t readTimestamp(std::istream & is);
const Sender * readSender(std::istream & is, DB & db);
const Sender * readNickSender(std::istream & is, DB & db);
void readOptionalReason(std::istream & is, std::string & reason);
bool readToEndDelim(std::istream & is, std::string &str, const char delim);
const Sender * nickSender(DB & db, const std::string & nick);
inline void ignoreTo(std::istream & is, const char delim = '\n');
uint64_t countLines(std::istream & is);


int main(int argc, char *argv[])
{
	if (argc != 5) {
		std::cout << "Usage: " << argv[0] << " <TextLog> <DB> <Network> <Buffer>" << std::endl;
		return EXIT_FAILURE;
	}

	std::cout << "Initializing..." << std::endl;

	std::ifstream is(argv[1], std::ios::in);
	DB db(argv[2]);

	uint64_t numLines = countLines(is);
	uint16_t bufferid = db.getBuffer(argv[3], argv[4], true)->id;

	std::cout << "Converting " << numLines << " entries..." << std::endl;

	uint64_t numDone = 0;
	time_t start = time(NULL);
	time_t t = start;
	db.beginSave();
	while (!is.eof() && is.good()) {
		Message msg;
		msg.bufferid = bufferid;
		if (time(NULL) - t >= 1) {
			db.endSave();
			t = time(NULL);
			std::cout
				<< " Converted " << numDone << '/' << numLines << " entries. "
				<< numDone / ((t - start) ? (t - start) : 1) << "/second"
				<< "        \r" << std::flush;
			db.beginSave();
		}
		bool add = readLine(is, db, msg, numDone);
		if (add) {
			db.addMessage(msg);
			numDone++;
		}
	}
	db.endSave();
	std::cout << "\nSucessfully converted " << numDone << " entries. " << std::endl;
	return EXIT_SUCCESS;
}


bool readLine(std::istream & is, DB & db, Message & msg, const uint64_t line)
{
	char c;

	msg.time = readTimestamp(is);
	assert(msg.time != -1);

	is.ignore(2);  // Ignore dual-space time delemiter
	// Peek at first character of message to find type
	is.get(c);

	if (is.eof()) {
		return false;
	}

	while (isdigit(c)) {
		// Hack to ignore common corruption of the form:
		// <TimeStamp> <TimeStamp> <...> <Nick> Hello!
		is.seekg(-1, std::ios::cur);
		msg.time = readTimestamp(is);
		assert(msg.time != -1);
		is.ignore(2).get(c);
		if (is.eof()) {
			return false;
		}
	}

	if (c == '<') {  // <Nick> Message.
		genericMessage(MessageType::Privmsg, is, db, msg, '>', 1);
	} else if (c == '-') {  // -Nick- Message.
		genericMessage(MessageType::Notice, is, db, msg, '-', 1);
	} else if (c == '*') {  // Action or special
		is.get(c);
		if (c == ' ') {  // Action
			genericMessage(MessageType::Action, is, db,
					msg, ' ', 0);
		} else if (c == '*') {  // Special
			is.ignore(2);  // Ignore "* "

			// These can't be determined from the begining
			// characters, so we'll have to read the whole line.
			std::string str;
			std::streampos pos = is.tellg();
			std::getline(is, str);
			is.seekg(pos);

			// XXX: These searches are probably slow, and can be broken with quit/part/kick
			// reasons.  (eg, `Nick <ident@host> has left #channel ("> has joined ")`)
			auto notFound = std::string::npos;
			if (str.find("> has joined ") != notFound) {  // Nick <ident@host> has joined #channel
				msg.type = MessageType::Join;
				msg.senderid = readSender(is, db)->id;
				ignoreTo(is);

			} else if (str.find("> has left ") != notFound) {  // Nick <ident@host> has left #channel (Reason)
				msg.type = MessageType::Part;
				msg.senderid = readSender(is, db)->id;
				readOptionalReason(is, msg.text);

			} else if (str.find(" was kicked by ") != notFound) {  // BadUser was kicked by Nick (Reason)
				msg.type = MessageType::Kick;
				msg.senderid = readNickSender(is, db)->id;
				is.ignore(14);  // Ignore "was kicked by "
				bool end = readToEndDelim(is, msg.text, ' ');
				if (!end) {
					std::string reason;
					readOptionalReason(is, reason);
					if (!reason.empty()) {
						msg.text += ' ';
						msg.text += reason;
					}
				}

			} else if (str.find("> has quit IRC") != notFound) {  // Nick <ident@host> has quit IRC (Quit: Reason)
				msg.type = MessageType::Quit;
				msg.senderid = readSender(is, db)->id;
				ignoreTo(is, '(');
				std::getline(is, msg.text);
				if (msg.text.size() > 0) msg.text.pop_back();

			} else if (str.find(" sets mode: ") != notFound) {  // Nick sets mode: +o Nick
				msg.type = MessageType::Mode;
				msg.senderid = readNickSender(is, db)->id;
				ignoreTo(is, ':');
				is.ignore(1);  // Ignore space
				std::getline(is, msg.text);

			} else if (str.find(" is now known as ") != notFound) {  // Nick1 is now known as Nick2
				msg.type = MessageType::Nick;
				const Sender * sender = readNickSender(is, db);
				msg.senderid = sender->id;
				is.ignore(16);  // Ignore "is now known as "
				std::getline(is, msg.text);
				// Generate a new sender with the old user and host
				Sender snd;
				snd.nick = msg.text;
				snd.user = sender->user;
				snd.host = sender->host;
				db.getSender(snd, true);  // Use getSender to prevent duplicates

			} else if (str.find(" changes topic to \"") != notFound) {  // Nick changes topic to ""
				msg.type = MessageType::Topic;
				msg.senderid = readNickSender(is, db)->id;
				is.ignore(18);  // Ignore "changes topic to \""
				std::getline(is, msg.text);
				if (msg.text.size() > 0) msg.text.pop_back();

			} else {
				goto corrupt;
			}
		} else {
			goto corrupt;
		}
	} else {
		corrupt:
		std::string s;
		std::getline(is, s);
		std::cout << "\nLog file corrupt near line " << line << ": " << s << std::endl;
		exit(EXIT_FAILURE);
	}
	return true;
}


void genericMessage(const MessageType type, std::istream & is, DB & db,
		Message & msg, const char delim, const short skipNum)
{
	std::string nick;
	msg.type = type;
	std::getline(is, nick, delim);
	msg.senderid = nickSender(db, nick)->id;
	is.ignore(skipNum);
	std::getline(is, msg.text);
}


time_t readTimestamp(std::istream & is)
{
	char buf[5];	 // Buffer for atoi
	tm t;	// Time struct

	// ISO 8601 combined date and time format (9999-12-31T23:59:59)
	is.get(buf, 5);
	t.tm_year = atoi(buf) - 1900;

	is.ignore().get(buf, 3);
	t.tm_mon = atoi(buf);

	is.ignore().get(buf, 3);
	t.tm_mday = atoi(buf);

	is.ignore().get(buf, 3);
	t.tm_hour = atoi(buf);

	is.ignore().get(buf, 3);
	t.tm_min = atoi(buf);

	is.ignore().get(buf, 3);
	t.tm_sec = atoi(buf);

	t.tm_isdst = false;

	return mktime(&t);
}


const Sender * readNickSender(std::istream & is, DB & db)
{
	std::string nick;
	std::getline(is, nick, ' ');
	return nickSender(db, nick);
}


inline void ignoreTo(std::istream & is, const char delim)
{
	std::string ign;
	std::getline(is, ign, delim);
}


const Sender * readSender(std::istream & is, DB & db)
{
	Sender snd;
	std::getline(is, snd.nick, ' ');
	is.ignore(1);  // Ignore "<"
	std::getline(is, snd.nick, '!');
	std::getline(is, snd.user, '@');
	std::getline(is, snd.host, '>');
	return db.getSender(snd, true);
}


void readOptionalReason(std::istream & is, std::string & reason)
{
	char c;
	while (is.get(c).good()) {
		if (c == '\n') {
			return;
		} else if (c == '(') {
			std::getline(is, reason);
			if (!reason.empty()) reason.pop_back();
			return;
		}
	}
}


bool readToEndDelim(std::istream & is, std::string &str, const char delim)
{
	char c;
	while (is.get(c).good()) {
		if (c == '\n') {
			return true;
		} else if (c == delim) {
			return false;
		}
		str += c;
	}
	return true;
}


const Sender * nickSender(DB & db, const std::string & nick)
{
	const Sender * sender = db.guessSenderByNick(nick);
	if (sender == NULL) {
		Sender snd;
		snd.nick = nick;
		sender = db.addSender(snd);
	}
	return sender;
}


uint64_t countLines(std::istream & is)
{
	std::string line;
	uint64_t numLines = 0;

	while (std::getline(is, line)) {
		numLines++;
	}

	is.clear();
	is.seekg(0);
	return numLines;
}

