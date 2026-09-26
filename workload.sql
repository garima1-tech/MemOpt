CREATE TABLE customer_data
(
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    age INTEGER,
    balance DOUBLE PRECISION
);

CREATE INDEX idx_customer_age
ON customer_data(age);

CREATE INDEX idx_customer_balance
ON customer_data(balance);

INSERT INTO customer_data
(name, age, balance)
SELECT
    'User_' || generate_series,
    (random() * 60 + 18)::INTEGER,
    random() * 100000
FROM generate_series(1, 100000);
