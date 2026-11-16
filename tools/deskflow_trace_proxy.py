#!/usr/bin/env python3
"""Temporary plain-TCP Deskflow proxy; logs pointer traffic, never key contents."""
import argparse
import asyncio
from collections import Counter
from datetime import datetime, timezone
import json
import struct


def event(kind, **values):
    print(json.dumps(dict(time=datetime.now(timezone.utc).isoformat(), event=kind, **values)), flush=True)


def pointer_fields(body):
    code = body[:4]
    if code in (b'DMMV', b'DMRM') and len(body) == 8:
        x, y = struct.unpack('>hh', body[4:])
        return dict(type=code.decode(), x=x, y=y)
    if code == b'CINN' and len(body) == 14:
        x, y, sequence, modifiers = struct.unpack('>hhIH', body[4:])
        return dict(type='CINN', x=x, y=y, sequence=sequence)
    if code == b'DINF' and len(body) == 18:
        x, y, width, height, zone, mx, my = struct.unpack('>hhHHHhh', body[4:])
        return dict(type='DINF', width=width, height=height, x=mx, y=my)
    if code in (b'COUT', b'CIAK', b'CBYE', b'QINF'):
        return dict(type=code.decode())
    return None


async def relay(reader, writer, direction, counts):
    while True:
        header = await reader.readexactly(4)
        size, = struct.unpack('>I', header)
        if not 4 <= size <= 4 * 1024 * 1024:
            raise ValueError('invalid Deskflow frame size')
        body = await reader.readexactly(size)
        writer.write(header + body)
        await writer.drain()
        counts[body[:4].decode('ascii', errors='replace')] += 1
        fields = pointer_fields(body)
        if fields:
            event('packet', direction=direction, **fields)


async def connected(reader, writer, options):
    upstream = None
    tasks = []
    counts = Counter()
    event('connected', peer=str(writer.get_extra_info('peername')))
    try:
        remote, upstream = await asyncio.open_connection(options.server, options.server_port)
        tasks = [asyncio.create_task(relay(reader, upstream, 'ESP->server', counts)),
                 asyncio.create_task(relay(remote, writer, 'server->ESP', counts))]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in done:
            task.result()
    except (OSError, ValueError, asyncio.IncompleteReadError) as error:
        event('closed', reason=type(error).__name__, packets=dict(counts))
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        for stream in (writer, upstream):
            if stream:
                stream.close()
                try:
                    await stream.wait_closed()
                except OSError:
                    pass


async def main(options):
    server = await asyncio.start_server(lambda r, w: connected(r, w, options), options.listen, options.port)
    event('listening', address=options.listen, port=options.port, server=options.server, server_port=options.server_port)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--listen', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=24801)
    parser.add_argument('--server', default='127.0.0.1')
    parser.add_argument('--server-port', type=int, default=24800)
    try:
        asyncio.run(main(parser.parse_args()))
    except KeyboardInterrupt:
        pass
