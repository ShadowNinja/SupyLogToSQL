
env = Environment(
	LIBPATH = ["."],
	LIBS = ["sqlite3"]
)

env.MergeFlags("--std=c++11 -O3 -Wall -Wextra")

env.Program("FromText", [
	"main.cpp",
	"IRCLog.cpp"
])

