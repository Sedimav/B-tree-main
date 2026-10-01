CREATE DATABASE demo_extra;
USE demo_extra;
CREATE TABLE orders (
  id INT INDEXED,
  client STRING DEFAULT "guest",
  amount INT DEFAULT 100,
  status STRING DEFAULT "new"
);

INSERT INTO orders (id, amount) VALUE
  (1, 250),
  (2, 80),
  (3, 500),
  (4, 40);

SELECT * FROM orders;

SELECT id, client, amount
FROM orders
WHERE (amount >= 100 AND status == "new") OR id == 4;

UPDATE orders
SET status = "vip"
WHERE id == 1 OR amount >= 500;

SELECT id, status FROM orders WHERE status == "vip";

SELECT COUNT(id) AS cnt,
       SUM(amount) AS total,
       AVG(amount) AS avg_amount
FROM orders
WHERE amount >= 80;