#!/usr/bin/env python3
"""ftp a file to the TT as the unprivileged user dev: asvput.py LOCAL REMOTE (binary).
REMOTE must be somewhere dev can write (/home/dev, /tmp, /work/dev)."""
import sys, os
from ftplib import FTP
pw = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '.asvpass-dev')).read().strip()
f = FTP(); f.connect(os.environ.get('ASV_HOST', 'asv-host'), 21, timeout=60); f.login('dev', pw)
f.storbinary('STOR ' + sys.argv[2], open(sys.argv[1], 'rb')); print(f.size(sys.argv[2]) if False else 'sent'); f.quit()
