namespace * pork

include "proto.thrift"

enum SyncOpType { SET_STATE, NEW_MSG }

enum MessageState { QUEUING, IN_PROGRESS, ACKED, FAILED }

struct MessageSdto {
  1: proto.Message msg,
  2: MessageState state,
  3: i32 n_deps,
}

struct DependencySdto {
  1: i32 n_resolved,
  2: list<proto.id_t> dependant_ids,
}

struct SnapshotSdto {
  1: map<proto.id_t, MessageSdto> all_msgs,
  2: map<string, DependencySdto> all_deps,
}

service BrokerInternal extends proto.Broker {
  void syncSnapshot(1: SnapshotSdto snapshot),
  oneway void syncSetMessageState(1: string queue_name, 2: proto.id_t msg_id, 3: MessageState state),
  oneway void syncAddMessages(1: string queue_name, 2: list<proto.Message> msgs, 3: list<proto.Dependency> deps),
}
