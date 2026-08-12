from pathlib import Path

import market_replay


def main():
    repo_root = Path(__file__).resolve().parents[1]
    config = market_replay.ReplayConfig.from_file(str(repo_root / "configs/example_config.kv"))
    result = market_replay.ReplayEngine(config).run()

    print(f"events={result.processed_event_count}")
    print(f"orders={result.order_count}")
    print(f"fills={result.fill_count}")
    print(f"ending_inventory={result.ending_inventory}")
    print(f"total_fees={result.total_fees}")
    print(f"final_book_hash={result.final_book_hash}")
    print(f"run_hash={result.run_hash}")


if __name__ == "__main__":
    main()
