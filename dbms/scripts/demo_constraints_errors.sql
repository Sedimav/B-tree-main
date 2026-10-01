CREATE DATABASE demo_errors;
USE demo_errors;

CREATE TABLE users (id INT INDEXED, login STRING INDEXED, age INT NOT NULL);
INSERT INTO users (id, login, age) VALUE (1, "alice", 20);

INSERT INTO users (id, login, age) VALUE (1, "duplicate_id", 21);
INSERT INTO users (id, login, age) VALUE (2, "alice", 22);
INSERT INTO users (id, login, age) VALUE (NULL, "null_id", 23);
INSERT INTO users (id, login) VALUE (3, "missing_age");
INSERT INTO users (id, login, age) VALUE ("bad_int", "bad", 24);
SELECT login FROM users WHERE login LIKE "[";
SELECT missing_column FROM users;

SELECT id, login, age FROM users WHERE id == 1;
