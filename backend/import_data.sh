#!/bin/bash
# ============================================================
# 家谱系统数据导入脚本（WSL / Linux 通用）
# 将 CSV 文件批量导入 MySQL 数据库
# ============================================================
# 用法:
#   ./import_data.sh <数据目录路径>
#
# 环境变量（可选）:
#   MYSQL_HOST      MySQL 主机地址（默认: 127.0.0.1）
#   MYSQL_PORT      MySQL 端口（默认: 3306）
#   MYSQL_USER      MySQL 用户名（默认: root）
#   MYSQL_PASSWORD  MySQL 密码（默认: 运行时提示输入）
#   MYSQL_DATABASE  数据库名称（默认: genealogy_db）
# ============================================================

set -e

# ---- 配置 ----
MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_DATABASE="${MYSQL_DATABASE:-genealogy_db}"

if [ -z "$MYSQL_PASSWORD" ]; then
    read -rsp "请输入 MySQL 密码 (用户=$MYSQL_USER): " MYSQL_PASSWORD
    echo ""
fi

MYSQL="mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $MYSQL_DATABASE"

# ---- 数据目录 ----
DATA_DIR="${1:-}"
if [ -z "$DATA_DIR" ]; then
    echo "用法: $0 <数据目录路径>"
    echo ""
    echo "数据目录中应包含以下 CSV 文件:"
    echo "  user.csv, genealogy.csv, member.csv, marriage.csv, genealogy_share.csv"
    echo ""
    echo "环境变量:"
    echo "  MYSQL_HOST / MYSQL_PORT / MYSQL_USER / MYSQL_PASSWORD / MYSQL_DATABASE"
    exit 1
fi

if [ ! -d "$DATA_DIR" ]; then
    echo "错误: 数据目录不存在: $DATA_DIR"
    exit 1
fi
DATA_DIR="$(cd "$DATA_DIR" && pwd)"

echo "=========================================="
echo "  家谱系统数据导入"
echo "=========================================="
echo "MySQL: $MYSQL_HOST:$MYSQL_PORT/$MYSQL_DATABASE"
echo "数据:  $DATA_DIR"
echo "=========================================="

# ---- 文件检查 ----
for f in user.csv genealogy.csv member.csv marriage.csv genealogy_share.csv; do
    if [ ! -f "$DATA_DIR/$f" ]; then
        echo "缺少文件: $f"; exit 1
    fi
    echo "  [OK] $f"
done

# ---- MySQL 连接检查 ----
if ! mysql -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" -p"$MYSQL_PASSWORD" -e "SELECT 1" >/dev/null 2>&1; then
    echo "错误: 无法连接 MySQL"; exit 1
fi

# ---- 导入 ----
M="$MYSQL"
D="$DATA_DIR"

echo ">>> 导入 User ..."
$M -e "LOAD DATA LOCAL INFILE '$D/user.csv' INTO TABLE User CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (user_id, username, password, status, created_at);"
echo "  用户: $($M -N -e 'SELECT COUNT(*) FROM User')"

echo ">>> 导入 Genealogy ..."
$M -e "LOAD DATA LOCAL INFILE '$D/genealogy.csv' INTO TABLE Genealogy CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (genealogy_id, name, surname, founder_name, create_time, description, user_id);"
echo "  族谱: $($M -N -e 'SELECT COUNT(*) FROM Genealogy')"

echo ">>> 导入 Member (可能需要几分钟) ..."
$M -e "SET FOREIGN_KEY_CHECKS = 0; LOAD DATA LOCAL INFILE '$D/member.csv' INTO TABLE Member CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (member_id, genealogy_id, name, gender, birth_date, death_date, biography, father_id, mother_id, spouse_id); SET FOREIGN_KEY_CHECKS = 1;"
echo "  成员: $($M -N -e 'SELECT COUNT(*) FROM Member')"

echo ">>> 导入 Marriage ..."
$M -e "LOAD DATA LOCAL INFILE '$D/marriage.csv' INTO TABLE Marriage CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (marriage_id, husband_id, wife_id, wedding_date, status, created_at);"
echo "  婚姻: $($M -N -e 'SELECT COUNT(*) FROM Marriage')"

echo ">>> 导入 GenealogyShare ..."
$M -e "LOAD DATA LOCAL INFILE '$D/genealogy_share.csv' INTO TABLE GenealogyShare CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (share_id, genealogy_id, user_id, permission, created_at);"
echo "  共享: $($M -N -e 'SELECT COUNT(*) FROM GenealogyShare')"

# ---- 后处理 ----
echo ""
echo "=== 后处理: generation & ancestor_path ==="

$M -e "UPDATE Member SET generation = 1 WHERE father_id IS NULL AND mother_id IS NULL;"
$M -e "WITH RECURSIVE gen_calc AS (SELECT member_id, 1 as gen FROM Member WHERE father_id IS NULL AND mother_id IS NULL UNION ALL SELECT m.member_id, gc.gen + 1 FROM Member m JOIN gen_calc gc ON (m.father_id = gc.member_id OR m.mother_id = gc.member_id)) UPDATE Member m INNER JOIN gen_calc gc ON m.member_id = gc.member_id SET m.generation = gc.gen WHERE m.generation IS NULL;" 2>/dev/null || true
echo "  最大辈分: 第 $($M -N -e 'SELECT MAX(generation) FROM Member') 代"

$M -e "UPDATE Member SET ancestor_path = CONCAT('/', member_id) WHERE father_id IS NULL AND mother_id IS NULL;"
for i in $(seq 1 30); do
    $M -e "UPDATE Member m JOIN Member p ON m.father_id = p.member_id SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;" 2>/dev/null
    $M -e "UPDATE Member m JOIN Member p ON m.mother_id = p.member_id SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;" 2>/dev/null
    remain=$($M -N -e "SELECT COUNT(*) FROM Member WHERE ancestor_path IS NULL" 2>/dev/null || echo 0)
    [ "$remain" -eq 0 ] && break
done
$M -e "UPDATE Member SET ancestor_path = CONCAT('/', member_id) WHERE ancestor_path IS NULL;"

echo ""
echo "=========================================="
echo "  导入完成！"
echo "=========================================="
$M -t -e "SELECT 'User' AS tbl, COUNT(*) AS cnt FROM User UNION ALL SELECT 'Genealogy', COUNT(*) FROM Genealogy UNION ALL SELECT 'Member', COUNT(*) FROM Member UNION ALL SELECT 'Marriage', COUNT(*) FROM Marriage UNION ALL SELECT 'GenealogyShare', COUNT(*) FROM GenealogyShare;"
