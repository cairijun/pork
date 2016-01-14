namespace * pork

enum MessageType { NORMAL, PUNCTUATION }

struct Message {
  1: i64 id,
  2: MessageType type,
  3: binary payload,
}

service Broker {
  Message getMessage(1: string topic, 2: i64 consumer_id),
  i64 addMessage(1: string topic, 2: Message message, 3: list<i64> deps),
  i64 addMessageGroup(1: string topic, 2: list<Message> messages, 3: list<i64> deps),
}
