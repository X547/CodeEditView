#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <Looper.h>
#include <Message.h>
#include <OS.h>

#include "DapClient.h"
#include "Json.h"


namespace Debugger {


TEST_CASE("JSON parsing and serialization")
{
	const std::optional<JsonValue> value = JsonValue::Parse(
		R"( {"seq": 3, "ok": true, "none": null, "pi": -1.5e1, "list": [1, "two", [], {}],)"
		R"( "text": "a\"b\\c\n\u00e9\ud83d\ude00"} )");
	REQUIRE(value.has_value());
	CHECK((*value)["seq"].AsInt() == 3);
	CHECK((*value)["ok"].AsBool());
	CHECK((*value)["none"].IsNull());
	CHECK((*value)["pi"].AsNumber() == -15);
	CHECK((*value)["list"].Size() == 4);
	CHECK((*value)["list"][1].AsString() == "two");
	CHECK((*value)["list"][2].IsArray());
	CHECK((*value)["list"][3].IsObject());
	CHECK((*value)["text"].AsString() == "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80");
	CHECK((*value)["missing"]["deeper"].IsNull());
	CHECK((*value)["list"][10].IsNull());

	CHECK_FALSE(JsonValue::Parse("{\"a\": }").has_value());
	CHECK_FALSE(JsonValue::Parse("[1, 2").has_value());
	CHECK_FALSE(JsonValue::Parse("1 2").has_value());
	CHECK_FALSE(JsonValue::Parse("\"\\x\"").has_value());

	JsonValue object;
	object.Set("b", 2).Set("a", "x\ty").Set("list", JsonValue::MakeArray());
	object.Set("b", 3.25);
	CHECK(object.Serialize() == R"({"b":3.25,"a":"x\ty","list":[]})");
	CHECK(JsonValue(int64_t(1) << 40).Serialize() == "1099511627776");
	CHECK(JsonValue::Parse(object.Serialize())->Serialize() == object.Serialize());
}


TEST_CASE("DAP framing")
{
	DapFrameReader reader;
	reader.Append("noise before\r\n");
	reader.Append(DapFrame("{\"a\":1}").substr(0, 10));
	CHECK_FALSE(reader.Next().has_value());
	reader.Append(DapFrame("{\"a\":1}").substr(10) + DapFrame("[]"));
	CHECK(reader.Next() == "{\"a\":1}");
	CHECK(reader.Next() == "[]");
	CHECK_FALSE(reader.Next().has_value());
	// Multi-byte text counts in bytes.
	reader.Append(DapFrame("\"\xc3\xa9\""));
	CHECK(reader.Next() == "\"\xc3\xa9\"");
}


class MessageCollector: public BLooper {
private:
	sem_id fSem;

public:
	std::vector<std::string> messages;
	bool closed {};

	MessageCollector():
		BLooper("MessageCollector"),
		fSem(create_sem(0, "messages"))
	{
	}

	~MessageCollector() override
	{
		delete_sem(fSem);
	}

	void MessageReceived(BMessage *message) override
	{
		if (message->what == kMsgDapMessage) {
			messages.push_back(message->GetString("json", ""));
		} else if (message->what == kMsgDapClosed) {
			closed = true;
		} else {
			BLooper::MessageReceived(message);
			return;
		}
		release_sem(fSem);
	}

	bool Wait()
	{
		return acquire_sem_etc(fSem, 1, B_RELATIVE_TIMEOUT, 2000000) == B_OK;
	}
};


TEST_CASE("DAP client exchanges messages with an adapter")
{
	MessageCollector *collector = new MessageCollector();
	collector->Run();
	{
		// "cat" echoes each request back.
		DapClient client {BMessenger(collector)};
		REQUIRE(client.Start({"cat"}) == B_OK);
		JsonValue arguments;
		arguments.Set("adapterID", "test");
		CHECK(client.Send("initialize", arguments) == 1);
		CHECK(client.Send("threads") == 2);
		REQUIRE(collector->Wait());
		REQUIRE(collector->Wait());
		client.Stop();
		REQUIRE(collector->Wait());
		CHECK_FALSE(client.IsRunning());
		CHECK(client.Send("threads") == -1);
	}
	collector->Lock();
	REQUIRE(collector->messages.size() == 2);
	const std::optional<JsonValue> first = JsonValue::Parse(collector->messages[0]);
	REQUIRE(first.has_value());
	CHECK((*first)["command"].AsString() == "initialize");
	CHECK((*first)["type"].AsString() == "request");
	CHECK((*first)["arguments"]["adapterID"].AsString() == "test");
	CHECK(JsonValue::Parse(collector->messages[1]).value()["seq"].AsInt() == 2);
	CHECK(collector->closed);
	collector->Quit();
}


} // Debugger
