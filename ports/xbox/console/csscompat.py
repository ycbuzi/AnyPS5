import re


def split_values(text):
    parts, depth, cur = [], 0, ""
    for ch in text.strip():
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch in " \t" and depth == 0:
            if cur:
                parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        parts.append(cur)
    return parts


def first_argument(text, start):
    depth, i = 0, start
    while i < len(text):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            if depth == 0:
                break
            depth -= 1
        elif text[i] == "," and depth == 0:
            return text[start:i].strip(), i
        i += 1
    return text[start:i].strip(), i


def drop_minmax(css):
    out, i = "", 0
    pattern = re.compile(r"(?<![\w-])(min|max)\(")
    while True:
        m = pattern.search(css, i)
        if not m:
            return out + css[i:]
        out += css[i:m.start()]
        arg, end = first_argument(css, m.end())
        depth, j = 0, end
        while j < len(css):
            if css[j] == "(":
                depth += 1
            elif css[j] == ")":
                if depth == 0:
                    break
                depth -= 1
            j += 1
        out += arg
        i = j + 1


def box(prefix, values):
    v = split_values(values)
    if len(v) == 1:
        v = v * 4
    elif len(v) == 2:
        v = [v[0], v[1], v[0], v[1]]
    elif len(v) == 3:
        v = [v[0], v[1], v[2], v[1]]
    return ";".join("%s%s:%s" % (prefix, side, value) for side, value in zip(("top", "right", "bottom", "left"), v[:4]))


def declarations(body):
    items, depth, cur = [], 0, ""
    for ch in body:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == ";" and depth == 0:
            items.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        items.append(cur)
    return items


def fix_rule(selector, body):
    decls = declarations(body)
    props = {}
    for d in decls:
        if ":" in d:
            k, v = d.split(":", 1)
            props[k.strip().lower()] = v.strip()
    new, extra = [], ""
    for d in decls:
        if ":" not in d:
            new.append(d)
            continue
        k, v = d.split(":", 1)
        key = k.strip().lower()
        value = v.strip()
        important = ""
        if value.endswith("!important"):
            important, value = "!important", value[:-10].strip()
        if key == "inset":
            new.append(";".join(p + important for p in box("", value).split(";")))
        elif key == "place-items":
            parts = split_values(value)
            new.append("align-items:%s%s;justify-items:%s%s" % (parts[0], important, parts[-1], important))
        elif key == "place-self":
            parts = split_values(value)
            new.append("align-self:%s%s;justify-self:%s%s" % (parts[0], important, parts[-1], important))
        elif key in ("gap", "row-gap", "column-gap"):
            display = props.get("display", "")
            parts = split_values(value)
            row, col = parts[0], parts[-1]
            if key == "row-gap":
                col = "0"
            if key == "column-gap":
                row = "0"
            if "grid" in display:
                new.append("grid-row-gap:%s;grid-column-gap:%s" % (row, col))
            elif "flex" in display:
                direction = props.get("flex-direction", "row")
                wrap = "wrap" in props.get("flex-wrap", "") or "wrap" in props.get("flex-flow", "")
                margin = "margin-top:%s" % row if direction.startswith("column") else "margin-left:%s" % col
                if wrap and not direction.startswith("column"):
                    margin += ";margin-bottom:%s" % row
                sels = [s.strip() for s in selector.split(",") if s.strip()]
                extra += "%s{%s}" % (",".join("%s>*+*" % s for s in sels), margin)
                if wrap and not direction.startswith("column"):
                    extra += "%s{margin-left:0}" % ",".join("%s>*:first-child" % s for s in sels)
            new.append(d)
        else:
            new.append(d)
    return "%s{%s}%s" % (selector, ";".join(new), extra)


def fix(css):
    css = drop_minmax(css)
    return re.sub(r"([^{}]+)\{([^{}]*)\}", lambda m: fix_rule(m.group(1), m.group(2)) if not m.group(1).strip().startswith("@") else m.group(0), css)
