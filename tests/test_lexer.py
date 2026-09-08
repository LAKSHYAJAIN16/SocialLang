from sociallang.lang.lexer import tokenize


def test_tokenizes_range_operator_distinct_from_decimal_number():
    tokens = tokenize("agents: 6..10")
    kinds_values = [(t.kind, t.value) for t in tokens if t.kind != "EOF"]
    assert kinds_values == [
        ("KEYWORD", "agents"), ("SYMBOL", ":"), ("NUMBER", "6"), ("SYMBOL", ".."), ("NUMBER", "10"),
    ]


def test_tokenizes_decimal_number_as_one_token():
    tokens = tokenize("0.995")
    assert [(t.kind, t.value) for t in tokens if t.kind != "EOF"] == [("NUMBER", "0.995")]


def test_tokenizes_string_with_escapes():
    tokens = tokenize('"line one\\nline two"')
    assert tokens[0].kind == "STRING"
    assert tokens[0].value == "line one\nline two"


def test_distinguishes_keywords_from_identifiers():
    tokens = tokenize("if role team")
    assert [t.kind for t in tokens if t.kind != "EOF"] == ["KEYWORD", "KEYWORD", "IDENT"]


def test_skips_line_comments():
    tokens = tokenize("let x = 1 // this is a comment\nlet y = 2")
    values = [t.value for t in tokens if t.kind != "EOF"]
    assert "comment" not in values
    assert values.count("let") == 2
