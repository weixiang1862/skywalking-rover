// Licensed to Apache Software Foundation (ASF) under one or more contributor
// license agreements. See the NOTICE file distributed with
// this work for additional information regarding copyright
// ownership. Apache Software Foundation (ASF) licenses this file to you under
// the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "api.h"
#include "socket.h"
#include "socket_opts.h"

#pragma once

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u32);
	__type(value, __u32);
} process_monitor_control SEC(".maps");
static __inline bool tgid_should_trace(__u32 tgid) {
    __u32 *val = bpf_map_lookup_elem(&process_monitor_control, &tgid);
    if (!val) {
        return false;
    }
    return (*val) == 1 ? true : false;
}

// tracepoint enter
struct trace_event_raw_sys_enter {
	long int id;
	long unsigned int args[6];
	char __data[0];
};

// tracepoint exit
struct trace_event_raw_sys_exit {
	long int id;
	long int ret;
	char __data[0];
};

// syscall:connect
struct connect_args_t {
    __u32 fd;
    struct sockaddr* addr;
    struct sock *sock;
    __u64 start_nacs;
};
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, struct connect_args_t);
} conecting_args SEC(".maps");

// syscall:close
struct sock_close_args_t {
    int fd;
    __u64 start_nacs;
};
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, struct sock_close_args_t);
} closing_args SEC(".maps");

// syscall:accept
struct accept_args_t {
    __u32 fd;
    struct sockaddr* addr;
    struct socket* socket;
    __u64 start_nacs;
};
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, struct accept_args_t);
} accepting_args SEC(".maps");

// syscall: data writing or receive
struct sock_data_args_t {
    __u32 fd;
    // current read/write is calls on the sockets.
    __u32 is_sock_event;
    size_t iovlen;
    unsigned int* msg_len;
    __u64 start_nacs;
    // rtt
    __u32 rtt_count;
    __u32 rtt_time;
    // buffer
    char* buf;
    struct iovec *iovec;
    __u64 data_id;
    // for openssl
    __u32 excepted_size;
    __u16 fix;
    __u8 ssl_buffer_force_unfinished;
    __u8 package_count;
    __u64 total_package_size;
    __u32 ifindex;
};
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, struct sock_data_args_t);
} socket_data_args SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, __u64);
} socket_data_id_generate_map SEC(".maps");
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, __u64);
} ssl_prepare_get_data_id_map SEC(".maps");
static __inline __u64 generate_socket_data_id(__u64 id, __u32 fd, __u32 func_name) {
    __u32 tgid = (__u32)(id >> 32);
    __u64 key = ((__u64)tgid << 32) | fd;
    if (tgid_should_trace(tgid) == false) {
        return 0;
    }
    __u64 *data_id = bpf_map_lookup_elem(&socket_data_id_generate_map, &key);
    if (!data_id) {
        __u64 tmp = 0;
        bpf_map_update_elem(&socket_data_id_generate_map, &key, &tmp, BPF_NOEXIST);
        data_id = bpf_map_lookup_elem(&socket_data_id_generate_map, &key);
        if (!data_id) {
            return 0;
        }
    }
    (*data_id)++;
    return *data_id;
}
static __inline __u64 ssl_get_data_id(__u8 from, __u64 id, __u32 fd) {
    __u32 tgid = (__u32)(id >> 32);
    __u64 key = ((__u64)tgid << 32) | fd;
    __u64 *data_id = bpf_map_lookup_elem(&socket_data_id_generate_map, &key);
    if (!data_id) {
        return 0;
    }
    return *data_id;
}

// syscall:sendfile, sendfile64
struct sendfile_args_t {
    __u32 out_fd;
    __u32 in_fd;
    size_t count;
    __u64 start_nacs;
    __u64 data_id;
};
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10000);
	__type(key, __u64);
	__type(value, struct sendfile_args_t);
} sendfile_args SEC(".maps");

struct socket_buffer_reader_t {
    __u32 data_len;
    char buffer[MAX_PROTOCOL_SOCKET_READ_LENGTH + 1];
};
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __type(key, __u32);
    __type(value, struct socket_buffer_reader_t);
    __uint(max_entries, 1);
} socket_buffer_reader_map SEC(".maps");
static __inline struct socket_buffer_reader_t* read_socket_data(struct sock_data_args_t *args, __u32 bytes_count) {
    __u64 size = 0;
    __u32 kZero = 0;
    char* buf;
    struct socket_buffer_reader_t* reader = bpf_map_lookup_elem(&socket_buffer_reader_map, &kZero);
    if (reader == NULL) {
        return NULL;
    }
    if (args->buf != NULL) {
        buf = args->buf;
        size = bytes_count;
    } else if (args->iovec != NULL) {
        struct iovec iov;
        bpf_probe_read(&iov, sizeof(iov), args->iovec);
        __u64 tmp = iov.iov_len;
        if (tmp > bytes_count) {
            tmp = bytes_count;
        }
        buf = (char *)iov.iov_base;
        size = tmp;
    }
    if (size <= 0) {
        return NULL;
    }
    if (size > MAX_PROTOCOL_SOCKET_READ_LENGTH) {
        size = MAX_PROTOCOL_SOCKET_READ_LENGTH;
    }
    asm volatile("%[size] &= 0x1f;\n" ::[size] "+r"(size) :);
    bpf_probe_read(&reader->buffer, size & MAX_PROTOCOL_SOCKET_READ_LENGTH, buf);
    reader->data_len = size & MAX_PROTOCOL_SOCKET_READ_LENGTH;
    return reader;
}
