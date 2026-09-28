import csv
import sqlite3
from pathlib import Path


conn = sqlite3.connect("shop.db")
cursor = conn.cursor()

csv_path = Path(__file__).resolve().parent.parent / "assets" / "csv" / "items.csv"



with csv_path.open(newline="", encoding="utf-8") as file:
    reader = csv.DictReader(file)

    for row in reader:
        cursor.execute(
            """
            INSERT OR IGNORE INTO products
            (id, name, description, price, stock, category, display)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                int(row["id"]),
                row["name"],
                row["description"],
                float(row["price"]),
                int(row["stock"]),
                row["category"],
                row["display"]
            )
        )


conn.commit()
conn.close()

print("Products imported successfully.")
