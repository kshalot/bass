import bluetooth
import sys
import os
import socket
from flask import Flask, render_template, redirect, request
from .repositories import NodeRepository
from .mqtt import MQTTClient
app = Flask(__name__)

app.config.from_object('config')

mqtt_client = MQTTClient()
mqtt_client.start()

node_repository = NodeRepository()


@app.errorhandler(404)
def not_found(error):
    return render_template('404.html'), 404


@app.route('/nodes/')
def nodes_index():
    return render_template('nodes/index.html', nodes=node_repository.all())


@app.route('/nodes/<node_id>/', methods=['GET'])
def nodes_show(node_id=0):
    return render_template('nodes/show.html', node=node_repository.find(node_id))


@app.route('/nodes/<node_id>/', methods=['POST'])
def nodes_update(node_id=0):
    errors = {}

    node = node_repository.find(node_id)

    new_volume, new_bass, new_mid, new_trebble = False, False, False, False

    def _check_range(value, min_value, max_value, comment):
        if value < min_value or value > max_value:
            errors[comment.lower()] = f"{comment}({value}) not between {min_value} and {max_value}"
            return False
        return True

    if (request.values['volume']):
        volume = int(request.values['volume'])
        if volume != node.volume and _check_range(volume, 0, 100, "Volume"):
            new_volume = True
            node.volume = volume

    if (request.values['bass']):
        bass = float(request.values['bass'])
        if bass != node.bass and _check_range(bass, -24, 12, "Bass"):
            new_bass = True
            node.bass = bass

    if (request.values['mid']):
        mid = float(request.values['mid'])
        if mid != node.mid and _check_range(mid, -24, 12, "Mid"):
            new_mid = True
            node.mid = mid

    if (request.values['trebble']):
        trebble = float(request.values['trebble'])
        if trebble != node.trebble and _check_range(trebble, -24, 12, "Trebble"):
            new_trebble = True
            node.trebble = trebble

    if not errors:
        node_hex_id = "{0:08X}"
        if new_volume:
            MQTTClient._publish_node_setting(mqtt_client.client, node_hex_id, 'volume', node.volume)
        if new_bass:
            MQTTClient._publish_node_setting(mqtt_client.client, node_hex_id, 'bass', node.bass)
        if new_mid:
            MQTTClient._publish_node_setting(mqtt_client.client, node_hex_id, 'mid', node.mid)
        if new_trebble:
            MQTTClient._publish_node_setting(mqtt_client.client, node_hex_id, 'trebble', node.trebble)
        node_repository.update(node)
        return render_template('nodes/show.html', node=node)
    else:
        return render_template('nodes/edit.html', node=node, errors=errors)


@app.route('/nodes/<node_id>/edit/')
def nodes_edit(node_id=0):
    return render_template('nodes/edit.html', node=node_repository.find(node_id), errors={})


@app.route('/bt/')
def bt():
    nearby_devices = bluetooth.discover_devices(
        duration=1,
        lookup_names=True,
        flush_cache=False,
        lookup_class=False
    )

    new_nodes = [
        (addr, name) for addr, name
        in nearby_devices if name[0:5] == 'node-'
    ]

    return render_template('bt/index.html', new_nodes=new_nodes)


@app.route('/bt/pair/<bt_addr>/')
def bt_pair(bt_addr, bt_port=1):
    def get_ip():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            # doesn't even have to be reachab
            s.connect(('10.255.255.255', 1))
            IP = s.getsockname()[0]
        except Exception:
            IP = '127.0.0.1'
        finally:
            s.close()
        return IP
    try:
        sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
        sock.connect((bt_addr, bt_port))
        print('Connected', file=sys.stderr)
        sock.send('\n'.join([
            f"1{os.getenv('WIFI_SSID')}",
            f"2{os.getenv('WIFI_PASSWORD')}",
            f"3{get_ip()}",
            ''
        ]))
    except Exception as e:
        print(e)
    finally:
        sock.close()
        return redirect('/bt/')


node_repository.create_database()
