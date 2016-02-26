namespace * pork

include "proto.thrift"

enum MessageState {
  QUEUING = 0,
  IN_PROGRESS = 1,
  FAILED = 2,
  ACKED = 3,
}

struct MessageSdto {
  1: proto.Message msg,
  2: MessageState state,
  3: i32 n_deps,
}

struct DependencySdto {
  1: i32 n_resolved,
  2: list<proto.id_t> dependant_ids,
}

struct QueueSdto {
  1: map<proto.id_t, MessageSdto> all_msgs,
  2: map<string, DependencySdto> all_deps,
}

struct SnapshotSdto {
  1: map<string, QueueSdto> queues;
}

service BrokerInternal extends proto.Broker {
  void syncSnapshot(1: SnapshotSdto snapshot),
  oneway void syncSetMessageState(1: string queue_name, 2: proto.id_t msg_id, 3: MessageState state),
  oneway void syncAddMessages(1: string queue_name, 2: list<proto.Message> msgs, 3: list<proto.Dependency> deps),
}
