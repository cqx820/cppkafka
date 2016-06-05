#include "consumer.h"
#include "exceptions.h"
#include "configuration.h"
#include "topic_partition_list.h"

using std::vector;
using std::string;
using std::move;

using std::chrono::milliseconds;

namespace cppkafka {

void Consumer::rebalance_proxy(rd_kafka_t*, rd_kafka_resp_err_t error,
                               rd_kafka_topic_partition_list_t *partitions, void *opaque) {
    TopicPartitionList list = convert(partitions);
    static_cast<Consumer*>(opaque)->handle_rebalance(error, list);
}

Consumer::Consumer(Configuration config) 
: KafkaHandleBase(move(config)) {
    char error_buffer[512];
    rd_kafka_conf_t* config_handle = get_configuration_handle();
    // Set ourselves as the opaque pointer
    rd_kafka_conf_set_opaque(config_handle, this);
    rd_kafka_conf_set_rebalance_cb(config_handle, &Consumer::rebalance_proxy);
    rd_kafka_t* ptr = rd_kafka_new(RD_KAFKA_CONSUMER, 
                                   rd_kafka_conf_dup(config_handle),
                                   error_buffer, sizeof(error_buffer));
    if (!ptr) {
        throw Exception("Failed to create consumer handle: " + string(error_buffer));
    }
    rd_kafka_poll_set_consumer(ptr);
    set_handle(ptr);
}

Consumer::~Consumer() {
    close();
}

void Consumer::set_assignment_callback(AssignmentCallback callback) {
    assignment_callback_ = move(callback);
}

void Consumer::set_revocation_callback(RevocationCallback callback) {
    revocation_callback_ = move(callback);
}

void Consumer::set_rebalance_error_callback(RebalanceErrorCallback callback) {
    rebalance_error_callback_ = move(callback);
}

void Consumer::subscribe(const vector<string>& topics) {
    TopicPartitionList topic_partitions(topics.begin(), topics.end());
    TopicPartitionsListPtr topic_list_handle = convert(topic_partitions);
    rd_kafka_resp_err_t error = rd_kafka_subscribe(get_handle(), topic_list_handle.get());
    check_error(error);
}

void Consumer::unsubscribe() {
    rd_kafka_resp_err_t error = rd_kafka_unsubscribe(get_handle());
    check_error(error);
}

void Consumer::assign(const TopicPartitionList& topic_partitions) {
    TopicPartitionsListPtr topic_list_handle = convert(topic_partitions);
    // If the list is empty, then we need to use a null pointer
    auto handle = topic_partitions.empty() ? nullptr : topic_list_handle.get();
    rd_kafka_resp_err_t error = rd_kafka_assign(get_handle(), handle);
    check_error(error);
}

void Consumer::unassign() {
    rd_kafka_resp_err_t error = rd_kafka_assign(get_handle(), nullptr);
    check_error(error);
}

void Consumer::close() {
    rd_kafka_resp_err_t error = rd_kafka_consumer_close(get_handle());
    check_error(error);
}

void Consumer::commit(const Message& msg) {
    commit(msg, false);
}

void Consumer::async_commit(const Message& msg) {
    commit(msg, true);
}

void Consumer::commit(const TopicPartitionList& topic_partitions) {
    commit(topic_partitions, false);
}

void Consumer::async_commit(const TopicPartitionList& topic_partitions) {
    commit(topic_partitions, true);
}

TopicPartitionList Consumer::get_offsets_committed(const TopicPartitionList& topic_partitions) {
    TopicPartitionsListPtr topic_list_handle = convert(topic_partitions);
    rd_kafka_resp_err_t error = rd_kafka_committed(get_handle(), topic_list_handle.get(),
                                                   get_timeout().count());
    check_error(error);
    return convert(topic_list_handle);
}

TopicPartitionList Consumer::get_offsets_position(const TopicPartitionList& topic_partitions) {
    TopicPartitionsListPtr topic_list_handle = convert(topic_partitions);
    rd_kafka_resp_err_t error = rd_kafka_position(get_handle(), topic_list_handle.get());
    check_error(error);
    return convert(topic_list_handle);
}

TopicPartitionList Consumer::get_subscription() {
    rd_kafka_resp_err_t error;
    rd_kafka_topic_partition_list_t* list = nullptr;
    error = rd_kafka_subscription(get_handle(), &list);
    check_error(error);
    return convert(make_handle(list));
}

TopicPartitionList Consumer::get_assignment() {
    rd_kafka_resp_err_t error;
    rd_kafka_topic_partition_list_t* list = nullptr;
    error = rd_kafka_assignment(get_handle(), &list);
    check_error(error);
    return convert(make_handle(list));
}

Message Consumer::poll() {
    rd_kafka_message_t* message = rd_kafka_consumer_poll(get_handle(), 
                                                         get_timeout().count());
    return message ? Message(message) : Message();
}

void Consumer::commit(const Message& msg, bool async) {
    rd_kafka_resp_err_t error;
    error = rd_kafka_commit_message(get_handle(), msg.get_handle(),
                                    async ? 1 : 0);
    check_error(error);
}

void Consumer::commit(const TopicPartitionList& topic_partitions, bool async) {
    TopicPartitionsListPtr topic_list_handle = convert(topic_partitions);
    rd_kafka_resp_err_t error;
    error = rd_kafka_commit(get_handle(), topic_list_handle.get(), async ? 1 : 0);
    check_error(error);
}

void Consumer::handle_rebalance(rd_kafka_resp_err_t error,
                                TopicPartitionList& topic_partitions) {
    if (error == RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS) {
        if (assignment_callback_) {
            assignment_callback_(topic_partitions);
        }
        assign(topic_partitions);
    }
    else if (error == RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS) {
        if (revocation_callback_) {
            revocation_callback_(topic_partitions);
        }
        unassign();
    }
    else {
        if (rebalance_error_callback_) {
            rebalance_error_callback_(error);
        }
        unassign();
    }
}

} // cppkafka
