#include "pa23.h"

void transfer(void *parent_data, local_id src, local_id dst, balance_t amount) {
  // student, please implement me
}

static int init_process(struct Self *self) {
  for (size_t i = 0; i < self->n_processes; ++i) {
    for (size_t j = 0; j < self->n_processes; ++j) {
      if (i == j)
        continue;
      int *pair = &self->pipes[2 * (i * self->n_processes + j)];
      if (j != self->id) {
        fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
                self->id, pair[0]);
        CHK_ERRNO(close(pair[0]));
      }
      if (i != self->id) {
        fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
                self->id, pair[1]);
        CHK_ERRNO(close(pair[1]));
      }
    }
  }
  return 0;
}

static int deinit_process(struct Self *self) {
  for (size_t i = 0; i < self->n_processes; ++i) {
    if (i != self->id) {
      int read_end = self->pipes[2 * (i * self->n_processes + self->id) + 0];
      int write_end = self->pipes[2 * (self->id * self->n_processes + i) + 1];
      fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
              self->id, read_end);
      CHK_ERRNO(close(read_end));
      fprintf(self->pipes_log, "Process %zu closing its pipe end %d\n",
              self->id, write_end);
      CHK_ERRNO(close(write_end));
    }
  }
  free(self->pipes);
  fclose(self->pipes_log);
  fclose(self->events_log);
  return 0;
}

static int wait_for_message(struct Self *self, size_t from, Message *msg,
                            MessageType type) {
  do {
    CHK_RETCODE(receive(self, (local_id)from, msg));
  } while (msg->s_header.s_type != type);
  return 0;
}

static int run_child(struct Self *self) {
  Message msg;
  CHK_RETCODE(init_process(self));

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = 0;
  msg.s_header.s_type = STARTED;
  msg.s_header.s_payload_len = snprintf(
      msg.s_payload, MAX_PAYLOAD_LEN, log_started_fmt, (int)get_physical_time(),
      (int)self->id, (int)getpid(), (int)getppid(), self->my_balance);
  fputs(msg.s_payload, self->events_log);
  CHK_RETCODE(send_multicast(self, &msg));

  for (size_t i = 1; i < self->n_processes; ++i) {
    if (i != self->id) {
      CHK_RETCODE(wait_for_message(self, i, &msg, STARTED));
    }
  }

  fprintf(self->events_log, log_received_all_started_fmt,
          (int)get_physical_time(), (int)self->id);

  msg.s_header.s_magic = MESSAGE_MAGIC;
  msg.s_header.s_local_time = 0;
  msg.s_header.s_type = DONE;
  msg.s_header.s_payload_len =
      snprintf(msg.s_payload, MAX_PAYLOAD_LEN, log_done_fmt,
               (int)get_physical_time(), (int)self->id, self->my_balance);
  fputs(msg.s_payload, self->events_log);
  CHK_RETCODE(send_multicast(self, &msg));

  for (size_t i = 1; i < self->n_processes; ++i) {
    if (i != self->id) {
      CHK_RETCODE(wait_for_message(self, i, &msg, DONE));
    }
  }

  fprintf(self->events_log, log_received_all_done_fmt, (int)get_physical_time(),
          (int)self->id);

  CHK_RETCODE(deinit_process(self));
  return 0;
}

static int run_parent(struct Self *self) {
  Message msg;
  CHK_RETCODE(init_process(self));

  // bank_robbery(parent_data);
  // print_history(all);

  for (size_t i = 1; i < self->n_processes; ++i)
    CHK_RETCODE(wait_for_message(self, i, &msg, STARTED));
  fprintf(self->events_log, log_received_all_started_fmt,
          (int)get_physical_time(), (int)self->id);

  for (size_t i = 1; i < self->n_processes; ++i)
    CHK_RETCODE(wait_for_message(self, i, &msg, DONE));
  fprintf(self->events_log, log_received_all_done_fmt, (int)get_physical_time(),
          (int)self->id);

  for (size_t i = 1; i < self->n_processes; ++i)
    wait(NULL);

  CHK_RETCODE(deinit_process(self));
  return 0;
}

struct Self self;

int main(int argc, char *argv[]) {
  size_t n_children;
  if (argc < 4 || sscanf(argv[2], "%zu", &n_children) != 1) {
    printf("Usage: %s -p <number of processes>"
           "<amount of money for each process>\n",
           argv[0]);
    return 0;
  }

  self.pipes_log = fopen(pipes_log, "w");
  self.events_log = fopen(events_log, "a");

  self.n_processes = n_children + 1;
  self.pipes = malloc(2 * sizeof(int) * self.n_processes * self.n_processes);
  for (size_t i = 0; i < self.n_processes; ++i) {
    for (size_t j = 0; j < self.n_processes; ++j) {
      int *to_create = &self.pipes[2 * (i * self.n_processes + j)];
      if (i == j) {
        to_create[0] = -1;
        to_create[1] = -1;
      } else {
        CHK_ERRNO(pipe(to_create));
        fprintf(self.pipes_log, "Pipe %zu ==> %zu: rfd=%d, wfd=%d\n", i, j,
                to_create[0], to_create[1]);
      }
    }
  }

  self.local_time = 0;

  for (size_t id = 1; id < self.n_processes; ++id) {
    sscanf(argv[2 + id], "%" SCNd16, &self.my_balance);
    pid_t pid = fork();
    CHK_ERRNO(pid);
    if (!pid) {
      self.id = id;
      return run_child(&self);
    }
  }

  self.id = 0;
  return run_parent(&self);
}
