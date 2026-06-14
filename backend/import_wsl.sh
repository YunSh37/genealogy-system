#!/bin/bash
# WSL MySQL data import script
MYSQL="mysql --local-infile=1 -u root -p123456 genealogy_db"
DATA="/mnt/d/shujuku/test_data"

echo "=== Importing User data ==="
$MYSQL -e "LOAD DATA LOCAL INFILE '$DATA/user.csv' INTO TABLE User CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (user_id, username, password, status, created_at);"
echo "Users: $($MYSQL -N -e 'SELECT COUNT(*) FROM User')"

echo "=== Importing Genealogy data ==="
$MYSQL -e "LOAD DATA LOCAL INFILE '$DATA/genealogy.csv' INTO TABLE Genealogy CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (genealogy_id, name, surname, founder_name, create_time, description, user_id);"
echo "Genealogies: $($MYSQL -N -e 'SELECT COUNT(*) FROM Genealogy')"

echo "=== Importing Member data (100K+ records, please wait) ==="
$MYSQL -e "SET FOREIGN_KEY_CHECKS = 0; LOAD DATA LOCAL INFILE '$DATA/member.csv' INTO TABLE Member CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (member_id, genealogy_id, name, gender, birth_date, death_date, biography, father_id, mother_id, spouse_id); SET FOREIGN_KEY_CHECKS = 1;"
echo "Members: $($MYSQL -N -e 'SELECT COUNT(*) FROM Member')"

echo "=== Importing Marriage data ==="
$MYSQL -e "LOAD DATA LOCAL INFILE '$DATA/marriage.csv' INTO TABLE Marriage CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (marriage_id, husband_id, wife_id, wedding_date, status, created_at);"
echo "Marriages: $($MYSQL -N -e 'SELECT COUNT(*) FROM Marriage')"

echo "=== Importing GenealogyShare data ==="
$MYSQL -e "LOAD DATA LOCAL INFILE '$DATA/genealogy_share.csv' INTO TABLE GenealogyShare CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (share_id, genealogy_id, user_id, permission, created_at);"
echo "Shares: $($MYSQL -N -e 'SELECT COUNT(*) FROM GenealogyShare')"

echo "=== Backfilling generation column ==="
$MYSQL -e "
UPDATE Member SET generation = 1 WHERE father_id IS NULL AND mother_id IS NULL;
"
echo "Root members: $($MYSQL -N -e 'SELECT COUNT(*) FROM Member WHERE generation = 1')"

# Use recursive CTE to fill generation values
$MYSQL -e "
WITH RECURSIVE gen_calc AS (
    SELECT member_id, 1 as gen FROM Member WHERE father_id IS NULL AND mother_id IS NULL
    UNION ALL
    SELECT m.member_id, gc.gen + 1
    FROM Member m
    JOIN gen_calc gc ON (m.father_id = gc.member_id OR m.mother_id = gc.member_id)
)
UPDATE Member m
INNER JOIN gen_calc gc ON m.member_id = gc.member_id
SET m.generation = gc.gen
WHERE m.generation IS NULL;
"
echo "Members with generation: $($MYSQL -N -e 'SELECT COUNT(*) FROM Member WHERE generation IS NOT NULL')"

echo "=== Generation distribution ==="
$MYSQL -e "SELECT generation, COUNT(*) as cnt FROM Member GROUP BY generation ORDER BY generation;"

echo "=== Import complete ==="