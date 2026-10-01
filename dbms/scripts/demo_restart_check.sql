USE demo_restart;

SELECT owner, balance FROM accounts WHERE id BETWEEN 1 AND 3;
SELECT id FROM accounts WHERE owner == "carol";
