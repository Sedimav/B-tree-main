CREATE DATABASE demo_grpc;
USE demo_grpc;

CREATE TABLE messages (id INT INDEXED, text STRING);
INSERT INTO messages (id, text) VALUE
  (1, "hello from grpc"),
  (2, "script mode works");

SELECT id, text FROM messages WHERE id BETWEEN 1 AND 3;
