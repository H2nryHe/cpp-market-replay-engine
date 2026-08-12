from pathlib import Path
import shutil

import market_replay


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "python_bindings"


def expect_raises(exc_type, fn):
    try:
        fn()
    except exc_type:
        return
    raise AssertionError(f"expected {exc_type.__name__}")


def clean(path):
    if path.exists():
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()


def run_example(output_name="example"):
    return market_replay.run_config_file(
        "configs/example_config.kv", str(BUILD / output_name), True
    )


def assert_golden(result):
    assert result.processed_event_count == 5
    assert result.book_update_count == 3
    assert result.trade_count == 2
    assert result.internal_event_count == 3
    assert result.strategy_intent_count == 2
    assert result.order_count == 2
    assert result.orders_canceled == 2
    assert result.fill_count == 3
    assert result.ending_cash == -50009
    assert result.ending_inventory == 5
    assert result.realized_gross_pnl == 0
    assert result.total_fees == 5
    assert result.turnover == 50004
    assert result.final_mid_x2 == 20002
    assert result.unrealized_gross_pnl_x2 == 2
    assert result.ending_equity_x2 == -8
    assert result.net_total_pnl_x2 == -8
    assert result.mark_available is True
    assert result.equity_determinable is True
    assert result.final_book_hash == "9ca1786003897355"
    assert result.run_hash == "8aca37583ca6f83a"


def test_import_and_basic_api():
    assert market_replay.__version__ == "0.1.0"
    config = market_replay.ReplayConfig.from_file("configs/example_config.kv")
    assert config.price_format == market_replay.PriceFieldFormat.Ticks
    assert config.price_format_name == "ticks"
    result = market_replay.ReplayEngine(config).run()
    assert_golden(result)


def test_cli_golden_equivalence_and_order_fill_access():
    result = run_example("golden_equivalence")
    assert_golden(result)

    orders = list(result.orders)
    fills = list(result.fills)
    assert len(orders) == 2
    assert len(fills) == 3
    assert (orders[0].order_id, orders[0].side_name, orders[0].order_type_name) == (
        1,
        "buy",
        "market",
    )
    assert (
        orders[1].order_id,
        orders[1].filled_quantity,
        orders[1].remaining_quantity,
        orders[1].limit_price_ticks,
        orders[1].status_name,
    ) == (2, 3, 3, 10000, "canceled")
    assert [
        (
            fill.fill_sequence_id,
            fill.order_id,
            fill.timestamp_ns,
            fill.side_name,
            fill.price_ticks,
            fill.quantity,
            fill.fee,
        )
        for fill in fills
    ] == [
        (1, 1, 150, "buy", 10002, 2, 2),
        (2, 2, 300, "buy", 10000, 2, 2),
        (3, 2, 350, "buy", 10000, 1, 1),
    ]


def test_100x_repeatability():
    expected = None
    for _ in range(100):
        config = market_replay.ReplayConfig.from_file("configs/example_config.kv")
        result = market_replay.ReplayEngine(config).run()
        hashes = (
            result.input_hash,
            result.config_hash,
            result.orders_hash,
            result.fills_hash,
            result.portfolio_hash,
            result.final_book_hash,
            result.run_hash,
        )
        if expected is None:
            expected = hashes
        assert hashes == expected


def test_same_content_different_path():
    copied = BUILD / "copied_inputs"
    clean(copied)
    copied.mkdir(parents=True)
    shutil.copyfile(ROOT / "tests/golden/e2e_book_updates.csv", copied / "book.csv")
    shutil.copyfile(ROOT / "tests/golden/e2e_trades.csv", copied / "trades.csv")

    original = run_example("original_content")
    config = market_replay.ReplayConfig.from_file("configs/example_config.kv")
    config.book_updates_path = str(copied / "book.csv")
    config.trades_path = str(copied / "trades.csv")
    copied_result = market_replay.ReplayEngine(config).run()
    assert copied_result.input_hash == original.input_hash
    assert copied_result.run_hash == original.run_hash


def test_error_paths_and_failed_run_recovery():
    expect_raises(ValueError, lambda: market_replay.load_config("tests/golden/e2e_invalid_config.kv"))

    def missing_input():
        config = market_replay.ReplayConfig.from_file("configs/example_config.kv")
        config.book_updates_path = "tests/golden/missing.csv"
        market_replay.ReplayEngine(config).run()

    for _ in range(3):
        expect_raises(RuntimeError, missing_input)

    output = BUILD / "output_exists"
    clean(output)
    market_replay.run_config_file("configs/example_config.kv", str(output), True)
    expect_raises(RuntimeError, lambda: market_replay.run_config_file("configs/example_config.kv", str(output), False))

    valid = market_replay.ReplayEngine(market_replay.ReplayConfig.from_file("configs/example_config.kv")).run()
    assert valid.run_hash == "8aca37583ca6f83a"


def test_optional_mark_none_and_exact_integers():
    one_sided_config = BUILD / "one_sided_config.kv"
    one_sided_config.parent.mkdir(parents=True, exist_ok=True)
    one_sided_config.write_text(
        "book_updates_path=tests/golden/e2e_one_sided_book_updates.csv\n"
        "trades_path=tests/golden/e2e_one_sided_trades.csv\n"
        "output_directory=build/python_bindings/one_sided_run\n"
        "price_format=ticks\n"
        "strategy_type=scripted\n"
        "scripted_intents=100:1:buy:market:1:\n"
        "order_latency_ns=0\n"
        "queue_fraction_ppm=0\n"
        "fee_rate_ppm=0\n"
        "initial_cash=0\n"
    )
    result = market_replay.run_config_file(str(one_sided_config), None, True)
    assert result.ending_inventory == 1
    assert result.mark_available is False
    assert result.equity_determinable is False
    assert result.final_mid_x2 is None
    assert result.ending_equity_x2 is None

    example = run_example("integer_fields")
    integer_fields = [
        example.processed_event_count,
        example.start_timestamp_ns,
        example.end_timestamp_ns,
        example.ending_cash,
        example.ending_inventory,
        example.final_mid_x2,
        example.orders[0].exchange_arrival_timestamp_ns,
        example.fills[0].price_ticks,
        example.fills[0].fee,
    ]
    assert all(isinstance(value, int) for value in integer_fields)


def test_typed_config_construction():
    config = market_replay.ReplayConfig()
    config.book_updates_path = "tests/golden/e2e_book_updates.csv"
    config.trades_path = "tests/golden/e2e_trades.csv"
    config.output_directory = "build/python_bindings/typed_config_run"
    config.price_format_name = "ticks"
    config.strategy_type = "scripted"
    first = market_replay.ScriptedIntent()
    first.trigger_key = market_replay.EventKey(100, 2)
    first.side = market_replay.Side.Buy
    first.order_type = market_replay.OrderType.Market
    first.quantity = 3
    second = market_replay.ScriptedIntent()
    second.trigger_key = market_replay.EventKey(200, 3)
    second.side = market_replay.Side.Buy
    second.order_type = market_replay.OrderType.Limit
    second.quantity = 6
    second.limit_price_ticks = 10000
    config.scripted_intents = [first, second]
    config.order_latency_ns = 50
    config.cancel_after_arrival_ns = 100
    config.queue_fraction_ppm = 500000
    config.fee_rate_ppm = 100
    result = market_replay.ReplayEngine(config).run()
    assert_golden(result)

    expect_raises(ValueError, lambda: setattr(config, "price_format_name", "floating"))


def main():
    clean(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)
    test_import_and_basic_api()
    test_cli_golden_equivalence_and_order_fill_access()
    test_100x_repeatability()
    test_same_content_different_path()
    test_error_paths_and_failed_run_recovery()
    test_optional_mark_none_and_exact_integers()
    test_typed_config_construction()


if __name__ == "__main__":
    main()
