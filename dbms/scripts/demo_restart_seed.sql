CREATE DATABASE demo_restart;
USE demo_restart;

CREATE TABLE accounts (id INT INDEXED, owner STRING INDEXED, balance INT NOT NULL);
INSERT INTO accounts (id, owner, balance) VALUE
  (1, "alice", 100),
  (2, "bob", 150),
  (3, "carol", 200);

SELECT id, owner, balance FROM accounts WHERE id >= 1;
