#!/usr/bin/env python3
"""Reject release ELF binaries above the supported glibc baseline."""
import argparse
import re
import subprocess


def required_versions(output):
    return {tuple(map(int, version.split('.'))) for version in re.findall(r'\bGLIBC_(\d+(?:\.\d+)+)\b', output)}


def check(path, maximum):
    output = subprocess.check_output(['readelf', '--version-info', '--wide', path], text=True)
    versions = required_versions(output)
    if not versions:
        raise ValueError(f'{path}: no glibc version requirements found')
    if max(versions) > maximum:
        raise ValueError(f'{path}: requires glibc {".".join(map(str,max(versions)))}; maximum is {".".join(map(str,maximum))}')
    dynamic = subprocess.check_output(['readelf', '--dynamic', '--wide', path], text=True)
    if 'libstdc++.so' in dynamic or 'libgcc_s.so' in dynamic:
        raise ValueError(f'{path}: C++ runtime must be linked statically for portable releases')
    print(f'{path}: glibc {".".join(map(str,max(versions)))} or older; C++ runtime linked statically')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--maximum', default='2.31')
    parser.add_argument('binaries', nargs='+')
    args = parser.parse_args()
    for binary in args.binaries:
        check(binary, tuple(map(int,args.maximum.split('.'))))
