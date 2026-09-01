+++
date = '2026-09-01T00:00:00Z'
draft = false
title = 'Hello, Linux'
summary = 'First post — what this blog is for, and a quick check that code blocks render.'
tags = ['meta', 'linux']
+++

Welcome. This blog is a place to write down the Linux things I'd otherwise forget: shell
one-liners, systemd units that finally worked, package-manager archaeology, and notes from
breaking (then fixing) my own machine.

Nothing fancy for the first post — just confirming that fenced code blocks get syntax
highlighting and a copy button:

```bash
# show the 10 largest packages installed via rpm-ostree layering
rpm -qa --queryformat '%{SIZE}\t%{NAME}\n' \
  | sort -rn \
  | head -10 \
  | numfmt --field=1 --to=iec
```

And a config snippet, because half of Linux is editing text files:

```ini
[Unit]
Description=Tidy /tmp weekly
After=network.target

[Timer]
OnCalendar=weekly
Persistent=true

[Install]
WantedBy=timers.target
```

More soon.
