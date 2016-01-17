namespace * pork

typedef i64 id_t

enum MessageType { NORMAL, PUNCTUATION }

struct Message {
  1: id_t id,
  2: MessageType type,
  3: binary payload,
}

service Broker {
  Message getMessage(1: string queue_name, 2: id_t last_msg),
  id_t addMessage(1: string queue_name, 2: Message message, 3: list<id_t> deps),
  id_t addMessageGroup(1: string queue_name, 2: list<Message> messages, 3: list<id_t> deps),
  oneway void ack(1: id_t msg_id),
  oneway void fail(1: id_t msg_id),
}
