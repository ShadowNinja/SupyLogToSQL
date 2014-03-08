
env = Environment(
	LIBPATH = ["."],
	LIBS = ["sqlite3"]
)

env.MergeFlags("--std=c++11 -g -O3")

env.StaticLibrary("IRCLog", Split("""
	IRCLog.cpp
"""))

env.Program("FromText", "main.cpp",
	LIBS = ["sqlite3", "IRCLog"]
)

