/* Some unit tests that don't require Redis to be running. */

#include "command.h"
#include "hiarray.h"
#include "hircluster.h"
#include "test_utils.h"
#include "win32.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper for the macro ASSERT_KEYS below. */
void check_keys(char **keys, int numkeys, struct cmd *command, char *file,
                int line) {
    int actual_numkeys = (int)hiarray_n(command->keys);
    if (actual_numkeys != numkeys) {
        fprintf(stderr, "%s:%d: Expected %d keys but got %d\n", file, line,
                numkeys, actual_numkeys);
        assert(actual_numkeys == numkeys);
    }
    for (int i = 0; i < numkeys; i++) {
        struct keypos *kpos = hiarray_get(command->keys, i);
        char *actual_key = kpos->start;
        int actual_keylen = (int)(kpos->end - kpos->start);
        if ((int)strlen(keys[i]) != actual_keylen ||
            strncmp(keys[i], actual_key, actual_keylen)) {
            fprintf(stderr,
                    "%s:%d: Expected key %d to be \"%s\" but got \"%.*s\"\n",
                    file, line, i, keys[i], actual_keylen, actual_key);
            assert(0);
        }
    }
}

/* Checks that a command (struct cmd *) has the given keys (strings). */
#define ASSERT_KEYS(command, ...)                                              \
    do {                                                                       \
        char *expected_keys[] = {__VA_ARGS__};                                 \
        size_t n = sizeof(expected_keys) / sizeof(char *);                     \
        check_keys(expected_keys, n, command, __FILE__, __LINE__);             \
    } while (0)

void test_redis_parse_error_nonresp(void) {
    struct cmd *c = command_get();
    c->cmd = strdup("+++Not RESP+++\r\n");
    c->clen = strlen(c->cmd);

    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_ERROR, "Unexpected parse success");
    ASSERT_MSG(!strcmp(c->errstr, "Command parse error"), c->errstr);
    command_destroy(c);
}

void test_redis_parse_cmd_get(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "GET foo");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_OK, "Parse not OK");
    ASSERT_KEYS(c, "foo");
    command_destroy(c);
}

void test_redis_parse_cmd_mset(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "MSET foo val1 bar val2");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_OK, "Parse not OK");
    ASSERT_KEYS(c, "foo", "bar");
    command_destroy(c);
}

void test_redis_parse_cmd_eval_1(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "EVAL dummyscript 1 foo");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_OK, "Parse not OK");
    ASSERT_KEYS(c, "foo");
    command_destroy(c);
}

void test_redis_parse_cmd_eval_0(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "EVAL dummyscript 0 foo");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_OK, "Parse not OK");
    ASSERT_MSG(hiarray_n(c->keys) == 0, "Nonzero number of keys");
    command_destroy(c);
}

void test_redis_parse_cmd_xgroup_no_subcommand(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "XGROUP");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_ERROR, "Unexpected parse success");
    ASSERT_MSG(!strcmp(c->errstr, "Unknown command XGROUP"), c->errstr);
    command_destroy(c);
}

void test_redis_parse_cmd_xgroup_destroy_no_key(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "xgroup destroy");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_MSG(c->result == CMD_PARSE_ERROR, "Parse not OK");
    const char *expected_error =
        "Failed to find keys of command XGROUP DESTROY";
    ASSERT_MSG(!strncmp(c->errstr, expected_error, strlen(expected_error)),
               c->errstr);
    command_destroy(c);
}

void test_redis_parse_cmd_xgroup_destroy_ok(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd, "xgroup destroy mystream mygroup");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_KEYS(c, "mystream");
    command_destroy(c);
}

void test_redis_parse_cmd_xreadgroup_ok(void) {
    struct cmd *c = command_get();
    /* Use group name and consumer name "streams" just to try to confuse the
     * parser. The parser shouldn't mistake those for the STREAMS keyword. */
    int len = redisFormatCommand(
        &c->cmd, "XREADGROUP GROUP streams streams COUNT 1 streams mystream >");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_KEYS(c, "mystream");
    command_destroy(c);
}

void test_redis_parse_cmd_xread_ok(void) {
    struct cmd *c = command_get();
    int len = redisFormatCommand(&c->cmd,
                                 "XREAD BLOCK 42 STREAMS mystream another $ $");
    ASSERT_MSG(len >= 0, "Format command error");
    c->clen = len;
    redis_parse_cmd(c);
    ASSERT_KEYS(c, "mystream");
    command_destroy(c);
}

int main(void) {
    test_redis_parse_error_nonresp();
    test_redis_parse_cmd_get();
    test_redis_parse_cmd_mset();
    test_redis_parse_cmd_eval_1();
    test_redis_parse_cmd_eval_0();
    test_redis_parse_cmd_xgroup_no_subcommand();
    test_redis_parse_cmd_xgroup_destroy_no_key();
    test_redis_parse_cmd_xgroup_destroy_ok();
    test_redis_parse_cmd_xreadgroup_ok();
    test_redis_parse_cmd_xread_ok();
    return 0;
}
