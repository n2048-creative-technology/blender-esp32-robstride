def format_status_bar(last_telem: dict, max_items: int = 3) -> str:
    if not last_telem:
        return "RobStride: -"
    items = list(last_telem.items())[:max_items]
    parts = []
    for mid, it in items:
        status = int(it.get('status_flags', 0))
        flags = []
        if status & 1: flags.append('E')
        if status & 2: flags.append('C')
        if status & 4: flags.append('W')
        if status & 8: flags.append('U')
        ftxt = ''.join(flags) or '-'
        parts.append(f"ID {mid} {ftxt} CAN 0x{int(it.get('last_can_id',0)):X}")
    return "RobStride: " + ' | '.join(parts)

