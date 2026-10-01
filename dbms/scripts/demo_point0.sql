CREATE DATABASE demo_main;
USE demo_main;

CREATE TABLE users (id INT INDEXED, login STRING INDEXED, age INT NOT NULL, city STRING);
INSERT INTO users (id, login, age, city) VALUE
  (1, "alice", 20, "Amsterdam"),
  (2, "bob", 25, "Paris"),
  (3, "carol", 30, NULL),
  (4, "anna", 28, "Rome");

SELECT * FROM users;
SELECT id AS user_id, login, age FROM users WHERE id BETWEEN 2 AND 4;
SELECT login, city FROM users WHERE login LIKE "a.*";

UPDATE users SET city = "Berlin" WHERE id == 2;
SELECT id, login, city FROM users WHERE id == 2;

DELETE FROM users WHERE login == "carol";
SELECT id, login FROM users WHERE id >= 1;

CREATE DATABASE archive;
USE archive;
CREATE TABLE events (event_id INT INDEXED, title STRING);
INSERT INTO events (event_id, title) VALUE (1, "created archive database");

SELECT login FROM demo_main.users WHERE id == 1;
SELECT title FROM archive.events WHERE event_id == 1;
