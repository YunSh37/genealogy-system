#!/usr/bin/env python3
# generate_test_data.py
"""
家谱系统模拟数据生成脚本
生成10个族谱，总成员数超过100,000
"""
import csv
import random
from datetime import datetime, timedelta
import os
from typing import List, Dict, Tuple, Set
import sys

class GenealogyDataGenerator:
    def __init__(self, output_dir: str = "data"):
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)
        
        # 姓氏列表
        self.surnames = ["张", "王", "李", "赵", "刘", "陈", "杨", "黄", "周", "吴"]
        
        # 男性名字
        self.male_names = ["伟", "强", "磊", "军", "勇", "超", "涛", "鹏", "杰", "浩",
                          "明", "亮", "文", "华", "国", "建", "平", "刚", "辉", "峰"]
        
        # 女性名字
        self.female_names = ["芳", "娜", "敏", "静", "丽", "艳", "娟", "霞", "婷", "雪",
                            "琳", "莹", "萍", "红", "梅", "洁", "玉", "英", "慧", "倩"]
        
        # 当前ID计数
        self.current_user_id = 1
        self.current_genealogy_id = 1
        self.current_member_id = 1
        
    def generate_users(self, count: int = 10) -> List[Dict]:
        """生成用户数据"""
        users = []
        for i in range(count):
            user = {
                "user_id": self.current_user_id,
                "username": f"user{i+1:03d}",
                "password": "123456",  # 简单密码
                "status": "active",
                "created_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            }
            users.append(user)
            self.current_user_id += 1
        return users
    
    def generate_genealogies(self, count: int = 10) -> List[Dict]:
        """生成族谱数据"""
        genealogies = []
        for i in range(count):
            surname = self.surnames[i % len(self.surnames)]
            genealogy = {
                "genealogy_id": self.current_genealogy_id,
                "name": f"{surname}氏族谱{i+1}",
                "surname": surname,
                "founder_name": f"{surname}氏先祖",
                "create_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "description": f"{surname}氏族谱，用于测试，包含至少30代传承关系",
                "user_id": 1  # 所有族谱属于第一个用户
            }
            genealogies.append(genealogy)
            self.current_genealogy_id += 1
        return genealogies
    
    def generate_member_name(self, surname: str, gender: str, generation: int) -> str:
        """生成成员姓名"""
        if gender == "male":
            name_char = random.choice(self.male_names)
        else:
            name_char = random.choice(self.female_names)
        
        # 根据代际添加辈分字（简化处理）
        generation_chars = ["一", "二", "三", "四", "五", "六", "七", "八", "九", "十",
                          "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
                          "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉"]
        
        if generation <= len(generation_chars):
            return f"{surname}{generation_chars[generation-1]}{name_char}"
        else:
            return f"{surname}{generation}{name_char}"
    
    def calculate_birth_date(self, generation: int) -> str:
        """计算出生日期，同一代内有差异"""
        # 基础出生年份
        base_year = 1900
        base_birth_year = base_year + (generation - 1) * 25
        
        # 在同一代内，按出生顺序有差异
        year_offset = -2+random.randint(0, 13)
        birth_year = base_birth_year + int(year_offset)
        
        # 随机月份和日期
        month = random.randint(1, 12)
        if month == 2:
            day = random.randint(1, 28)
        elif month in [4, 6, 9, 11]:
            day = random.randint(1, 30)
        else:
            day = random.randint(1, 31)
        
        return f"{birth_year:04d}-{month:02d}-{day:02d}"
    
    def calculate_death_date(self, birth_date: str) -> str:
        """计算死亡日期，寿命在60-100岁之间"""
        birth = datetime.strptime(birth_date, "%Y-%m-%d")
        lifespan = random.randint(60, 100)  # 随机寿命
        death = birth + timedelta(days=lifespan*365)
        return death.strftime("%Y-%m-%d")
    
    def generate_large_genealogy_members(self, genealogy_id: int, surname: str, 
                                        total_members: int = 50000, 
                                        generations: int = 30) -> List[Dict]:
        """生成大型族谱成员（50,000+成员，30代）"""
        members = []
        member_map = {}  # generation -> list of member_ids
        
        # 计算每代大致人数
        members_per_gen = total_members // generations
        
        # 第一代：始祖夫妇
        for i in range(2):  # 始祖夫妇
            gender = "male" if i == 0 else "female"
            member = {
                "member_id": self.current_member_id,
                "genealogy_id": genealogy_id,
                "name": self.generate_member_name(surname, gender, 1),
                "gender": gender,
                "birth_date": self.calculate_birth_date(1),
                "death_date": self.calculate_death_date(self.calculate_birth_date(1)),
                "biography": f"{surname}氏第1代{gender}性成员",
                "father_id": None,
                "mother_id": None,
                "spouse_id": None
            }
            
            # 如果是始祖夫妇，互相设置配偶
            if i == 0:  # 丈夫
                founder_husband_id = self.current_member_id
            else:  # 妻子
                founder_wife_id = self.current_member_id
                # 互相设置配偶
                members[-1]["spouse_id"] = founder_wife_id
                member["spouse_id"] = founder_husband_id
            
            members.append(member)
            member_map.setdefault(1, []).append(self.current_member_id)
            self.current_member_id += 1
        
        # 第二代及以后
        for gen in range(2, generations + 1):
            # 上一代的成员作为父母
            if gen-1 in member_map:
                parents = member_map[gen-1]
            else:
                parents = []
            
            # 这一代的人数
            current_gen_count = members_per_gen
            
            # 生成这一代的成员
            for i in range(current_gen_count):
                # 随机选择父母
                father_id = None
                mother_id = None
                if parents and len(parents) >= 2:
                    # 随机选择一对夫妻作为父母
                    parent1_idx = random.randint(0, len(parents)-1)
                    parent2_idx = random.randint(0, len(parents)-1)
                    while parent2_idx == parent1_idx:
                        parent2_idx = random.randint(0, len(parents)-1)
                    
                    # 假设第一个是父亲，第二个是母亲
                    father_id = parents[parent1_idx]
                    mother_id = parents[parent2_idx]
                
                # 随机性别
                gender = "male" if random.random() > 0.5 else "female"
                
                member = {
                    "member_id": self.current_member_id,
                    "genealogy_id": genealogy_id,
                    "name": self.generate_member_name(surname, gender, gen),
                    "gender": gender,
                    "birth_date": self.calculate_birth_date(gen),
                    "death_date": self.calculate_death_date(self.calculate_birth_date(gen)),
                    "biography": f"{surname}氏第{gen}代{gender}性成员",
                    "father_id": father_id,
                    "mother_id": mother_id,
                    "spouse_id": None
                }
                
                members.append(member)
                member_map.setdefault(gen, []).append(self.current_member_id)
                self.current_member_id += 1
            
            # 为这一代的成员随机配对（配偶关系）
            current_gen_members = member_map[gen]
            males = [m for m in current_gen_members 
                    if next((mem for mem in members if mem["member_id"] == m), {}).get("gender") == "male"]
            females = [m for m in current_gen_members 
                      if next((mem for mem in members if mem["member_id"] == m), {}).get("gender") == "female"]
            
            # 随机配对
            random.shuffle(males)
            random.shuffle(females)
            
            pairs = min(len(males), len(females))
            for j in range(pairs):
                husband_id = males[j]
                wife_id = females[j]
                
                # 找到对应的成员并设置配偶
                for member in members:
                    if member["member_id"] == husband_id:
                        member["spouse_id"] = wife_id
                    elif member["member_id"] == wife_id:
                        member["spouse_id"] = husband_id
        
        return members
    
    def generate_medium_genealogy_members(self, genealogy_id: int, surname: str, 
                                         total_members: int = 6000, 
                                         generations: int = 30) -> List[Dict]:
        """生成中型族谱成员"""
        return self.generate_large_genealogy_members(
            genealogy_id, surname, total_members, generations
        )
    
    def write_csv(self, filename: str, data: List[Dict], fieldnames: List[str]):
        """写入CSV文件"""
        filepath = os.path.join(self.output_dir, filename)
        with open(filepath, 'w', newline='', encoding='utf-8') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(data)
        print(f"✓ 已生成 {filename}: {len(data)} 条记录")
    
    def generate_all_data(self):
        """生成所有测试数据"""
        print("开始生成测试数据...")
        
        # 1. 生成用户数据
        users = self.generate_users(10)
        self.write_csv("user.csv", users, ["user_id", "username", "password", "status", "created_at"])
        
        # 2. 生成族谱数据
        genealogies = self.generate_genealogies(10)
        self.write_csv("genealogy.csv", genealogies, 
                      ["genealogy_id", "name", "surname", "founder_name", "create_time", "description", "user_id"])
        
        # 3. 生成成员数据
        all_members = []
        
        # 第一个族谱：大型，50,000+成员
        print(f"\n生成第1个族谱成员数据（50,000+成员，30代）...")
        large_members = self.generate_large_genealogy_members(
            genealogy_id=1,
            surname=self.surnames[0],
            total_members=50000,
            generations=30
        )
        all_members.extend(large_members)
        
        # 其他9个族谱：中型，每个约6,000成员
        for i in range(2, 11):
            print(f"\n生成第{i}个族谱成员数据（约6,000成员，30代）...")
            medium_members = self.generate_medium_genealogy_members(
                genealogy_id=i,
                surname=self.surnames[(i-1) % len(self.surnames)],
                total_members=6000,
                generations=30
            )
            all_members.extend(medium_members)
        
        # 写入成员CSV文件
        self.write_csv("member.csv", all_members, 
                      ["member_id", "genealogy_id", "name", "gender", "birth_date", 
                       "death_date", "biography", "father_id", "mother_id", "spouse_id"])
        
        # 4. 生成婚姻数据（从成员数据中提取）
        print("\n从成员数据中提取婚姻关系...")
        marriages = []
        marriage_id = 1
        
        # 使用集合记录已处理的婚姻，避免重复
        processed_marriages = set()
        
        for member in all_members:
            if member["spouse_id"] and member["member_id"] < member["spouse_id"]:
                # 确保每对婚姻只记录一次
                marriage_key = (member["member_id"], member["spouse_id"])
                if marriage_key not in processed_marriages:
                    # 确定丈夫和妻子
                    if member["gender"] == "male":
                        husband_id = member["member_id"]
                        wife_id = member["spouse_id"]
                    else:
                        husband_id = member["spouse_id"]
                        wife_id = member["member_id"]
                    
                    # 计算结婚日期（假设比两人出生日期晚20年）
                    husband_birth = datetime.strptime(
                        next(m["birth_date"] for m in all_members if m["member_id"] == husband_id), 
                        "%Y-%m-%d"
                    )
                    wedding_date = (husband_birth + timedelta(days=20 * 365)).strftime("%Y-%m-%d")
                    
                    marriage = {
                        "marriage_id": marriage_id,
                        "husband_id": husband_id,
                        "wife_id": wife_id,
                        "wedding_date": wedding_date,
                        "status": "active",
                        "created_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                    }
                    marriages.append(marriage)
                    marriage_id += 1
                    processed_marriages.add(marriage_key)
        
        self.write_csv("marriage.csv", marriages, 
                      ["marriage_id", "husband_id", "wife_id", "wedding_date", "status", "created_at"])
        
        # 5. 生成族谱共享数据
        print("\n生成族谱共享数据...")
        shares = []
        share_id = 1
        
        # 第一个用户（user_id=1）创建了所有族谱
        # 为其他用户添加共享权限
        for genealogy_id in range(1, 11):
            for user_id in range(2, 6):  # 用户2-5有查看或编辑权限
                share = {
                    "share_id": share_id,
                    "genealogy_id": genealogy_id,
                    "user_id": user_id,
                    "permission": "edit" if user_id % 2 == 0 else "view",
                    "created_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                }
                shares.append(share)
                share_id += 1
        
        self.write_csv("genealogy_share.csv", shares, 
                      ["share_id", "genealogy_id", "user_id", "permission", "created_at"])
        
        # 打印统计信息
        print("\n" + "="*50)
        print("数据生成完成！")
        print("="*50)
        print(f"用户总数: {len(users)}")
        print(f"族谱总数: {len(genealogies)}")
        print(f"成员总数: {len(all_members)}")
        print(f"婚姻记录: {len(marriages)}")
        print(f"共享记录: {len(shares)}")
        print(f"大型族谱（ID=1）成员数: {len(large_members)}")
        print(f"数据文件保存在目录: {self.output_dir}/")
        print("="*50)

def main():
    """主函数"""
    try:
        generator = GenealogyDataGenerator(output_dir="test_data")
        generator.generate_all_data()
        
        # 生成导入脚本
        generate_import_script()
        
    except Exception as e:
        print(f"生成数据时出错: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

def generate_import_script():
    """生成MySQL导入脚本"""
    script_content = """#!/bin/bash
# import_data_fixed_local.sh
echo "=== 开始在WSL Ubuntu中导入数据 ==="

# MySQL连接参数
MYSQL_HOST="127.0.0.1"
MYSQL_PORT="3306"
MYSQL_USER="root"
DATABASE="genealogy_db"

# 数据目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/test_data"

# 如果没有指定密码，提示输入
if [ -z "$MYSQL_PASSWORD" ]; then
    read -sp "请输入MySQL root密码: " MYSQL_PASSWORD
    echo ""
fi

# 检查数据目录是否存在
if [ ! -d "$DATA_DIR" ]; then
    echo "错误: 数据目录不存在: $DATA_DIR"
    echo "请先运行数据生成脚本"
    exit 1
fi

echo "检查CSV文件..."
for file in "user.csv" "genealogy.csv" "member.csv" "marriage.csv" "genealogy_share.csv"; do
    if [ ! -f "$DATA_DIR/$file" ]; then
        echo "错误: 找不到文件 $DATA_DIR/$file"
        exit 1
    fi
    echo "✓ 找到 $file"
done

echo ""

# 启用本地文件加载
echo "启用本地文件加载..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD -e "SET GLOBAL local_infile = 1;" 2>/dev/null || true

# 1. 导入用户数据
echo "1. 导入用户数据..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
LOAD DATA LOCAL INFILE '$DATA_DIR/user.csv'
INTO TABLE User
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
ENCLOSED BY '"'
LINES TERMINATED BY '\\n'
IGNORE 1 ROWS
(user_id, username, password, status, created_at);
EOF

if [ $? -eq 0 ]; then
    echo "✓ 用户数据导入成功"
else
    echo "✗ 用户数据导入失败"
fi

# 2. 导入族谱数据
echo "2. 导入族谱数据..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
LOAD DATA LOCAL INFILE '$DATA_DIR/genealogy.csv'
INTO TABLE Genealogy
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
ENCLOSED BY '"'
LINES TERMINATED BY '\\n'
IGNORE 1 ROWS
(genealogy_id, name, surname, founder_name, create_time, description, user_id);
EOF

if [ $? -eq 0 ]; then
    echo "✓ 族谱数据导入成功"
else
    echo "✗ 族谱数据导入失败"
fi

# 3. 导入成员数据（这可能需要几分钟）
echo "3. 导入成员数据（这可能需要几分钟，请耐心等待）..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
SET FOREIGN_KEY_CHECKS = 0;
LOAD DATA LOCAL INFILE '$DATA_DIR/member.csv'
INTO TABLE Member
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
ENCLOSED BY '"'
LINES TERMINATED BY '\\n'
IGNORE 1 ROWS
(member_id, genealogy_id, name, gender, birth_date, death_date, biography, 
 father_id, mother_id, spouse_id);
SET FOREIGN_KEY_CHECKS = 1;
EOF

if [ $? -eq 0 ]; then
    echo "✓ 成员数据导入成功"
else
    echo "✗ 成员数据导入失败"
fi

# 4. 导入婚姻数据
echo "4. 导入婚姻数据..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
LOAD DATA LOCAL INFILE '$DATA_DIR/marriage.csv'
INTO TABLE Marriage
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
ENCLOSED BY '"'
LINES TERMINATED BY '\\n'
IGNORE 1 ROWS
(marriage_id, husband_id, wife_id, wedding_date, status, created_at);
EOF

if [ $? -eq 0 ]; then
    echo "✓ 婚姻数据导入成功"
else
    echo "✗ 婚姻数据导入失败"
fi

# 5. 导入族谱共享数据
echo "5. 导入族谱共享数据..."
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
LOAD DATA LOCAL INFILE '$DATA_DIR/genealogy_share.csv'
INTO TABLE GenealogyShare
CHARACTER SET utf8mb4
FIELDS TERMINATED BY ','
ENCLOSED BY '"'
LINES TERMINATED BY '\\n'
IGNORE 1 ROWS
(share_id, genealogy_id, user_id, permission, created_at);
EOF

if [ $? -eq 0 ]; then
    echo "✓ 共享数据导入成功"
else
    echo "✗ 共享数据导入失败"
fi

echo ""
echo "=== 数据导入完成 ==="
echo ""

# 显示统计信息
echo "数据库统计信息："
mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $DATABASE << EOF
SELECT 
    'User' as 表名,
    COUNT(*) as 记录数
FROM User
UNION ALL
SELECT 
    'Genealogy',
    COUNT(*)
FROM Genealogy
UNION ALL
SELECT 
    'Member',
    COUNT(*)
FROM Member
UNION ALL
SELECT 
    'Marriage',
    COUNT(*)
FROM Marriage
UNION ALL
SELECT 
    'GenealogyShare',
    COUNT(*)
FROM GenealogyShare;
EOF
"""
    
    with open("import_data.sh", "w", encoding="utf-8") as f:
        f.write(script_content)
    
    # 使脚本可执行
    os.chmod("import_data.sh", 0o755)
    
    print("✓ 已生成导入脚本: import_data.sh")
    print("  使用命令: ./import_data.sh 导入数据")

if __name__ == "__main__":
    main()