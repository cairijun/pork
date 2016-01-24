namespace * pork

typedef i64 id_t

enum MessageType { NORMAL, PUNCTUATION }

struct Dependency {
  1: string key,
  2: i32 n,
}

struct Message {
  1: optional id_t id,
  2: optional string resolve_dep,
  3: MessageType type,
  4: binary payload,
}

exception Timeout {}

service Broker {
  Message getMessage(1: string queue_name, 2: id_t last_msg) throws (1:Timeout e),
  id_t addMessage(1: string queue_name, 2: Message message, 3: list<Dependency> deps),
  list<id_t> addMessageGroup(1: string queue_name, 2: list<Message> messages, 3: list<Dependency> deps),
  oneway void ack(1: string queue_name, 2: id_t msg_id),
  oneway void fail(1: string queue_name, 2: id_t msg_id),
}
