#!/usr/bin/env python

import os
import sys
import signal
import logging
import ConfigParser
import ow
import time
import paho.mqtt.client as mqtt
import argparse

parser = argparse.ArgumentParser( formatter_class=argparse.RawDescriptionHelpFormatter,  
description='''glues between one wire and mqtt stuff''')
parser.add_argument('config_file', metavar="<config_file>", help="file with configuration")
args = parser.parse_args()


# read and parse config file
config = ConfigParser.RawConfigParser()
config.read(args.config_file)
# [mqtt]
MQTT_HOST = config.get("mqtt", "host")
MQTT_PORT = config.getint("mqtt", "port")
STATUSTOPIC = config.get("mqtt", "statustopic")
POLLINTERVAL = config.getint("mqtt", "pollinterval")
# [Onewire]
OW_HOST = config.get("onewire", "host")
OW_PORT = config.get("onewire", "port")
# [log]
LOGFILE = config.get("log", "logfile")
VERBOSE = config.get("log", "verbose")
# [sensors]
section_name = "sensors"
sensors = {}
for name, value in config.items(section_name):
	sensors[name] = value
# [switch]
section_name = "switches"
switches = {}
for name, value in config.items(section_name):
	switches[name] = value

APPNAME = "ow2mqtt"

# init logging 
LOGFORMAT = '%(asctime)-15s %(message)s'
if VERBOSE:
	logging.basicConfig(filename=LOGFILE, format=LOGFORMAT, level=logging.DEBUG)
else:
	logging.basicConfig(filename=LOGFILE, format=LOGFORMAT, level=logging.INFO)

logging.info("Starting " + APPNAME)
if VERBOSE:
	logging.info("INFO MODE")
else:
	logging.debug("DEBUG MODE")

# MQTT
MQTT_CLIENT_ID = APPNAME + "_%d" % os.getpid()
mqttc = mqtt.Client(MQTT_CLIENT_ID)

# 0: Connection successful 
# 1: Connection refused - incorrect protocol version
# 2: Connection refused - invalid client identifier
# 3: Connection refused - server unavailable
# 4: Connection refused - bad username or password
# 5: Connection refused - not authorised
# 6-255: Currently unused.
def mqtt_on_connect(client, userdata, flags, return_code):
	logging.debug("mqtt_on_connect return_code: " + str(return_code))
	if return_code == 0:
		logging.info("Connected to %s:%s", MQTT_HOST, MQTT_PORT)
		# set Lastwill 
        	mqttc.publish(STATUSTOPIC, "connected", retain=True)
	
		#Subscribing to topics
		for owid, owtopic in switches.items():
			mqttc.subscribe(owtopic + '/set')

	elif return_code == 1:
		logging.info("Connection refused - unacceptable protocol version")
		cleanup()
	elif return_code == 2:
		logging.info("Connection refused - identifier rejected")
		cleanup()
	elif return_code == 3:
		logging.info("Connection refused - server unavailable")
		logging.info("Retrying in 10 seconds")
		time.sleep(10)
	elif return_code == 4:
		logging.info("Connection refused - bad user name or password")
		cleanup()
	elif return_code == 5:
		logging.info("Connection refused - not authorised")
		cleanup()
	else:
		logging.warning("Something went wrong. RC:" + str(return_code))
		cleanup()

def mqtt_on_disconnect(mosq, obj, return_code):
	if return_code == 0:
		logging.info("Clean disconnection")
	else:
		logging.info("Unexpected disconnection. Reconnecting in 5 seconds")
		logging.debug("return_code: %s", return_code)
		time.sleep(5)

def mqtt_on_log(mosq, obj, level, string):
	if VERBOSE:    
		logging.debug(string)

def mqtt_on_publish(mosq, obj, mid):
	logging.debug("MID " + str(mid) + " published.")

def mqtt_on_message(client, userdata, msg):
	print(msg.topic + " " + str(msg.payload))

# clean disconnect on SIGTERM or SIGINT. 
def cleanup(signum, frame):
	logging.info("Disconnecting from broker")
	# Publish a retained message to state that this client is offline
	mqttc.publish(STATUSTOPIC, "0 - DISCONNECT", retain=True)
	mqttc.disconnect()
	mqttc.loop_stop()
	logging.info("Exiting on signal %d", signum)
	sys.exit(signum)

# Main Loop
def main_loop():
	logging.debug(("onewire server : %s") % (OW_HOST))    
	logging.debug(("  port         : %s") % (str(OW_PORT)))
	logging.debug(("MQTT broker    : %s") % (MQTT_HOST))
	logging.debug(("  port         : %s") % (str(MQTT_PORT)))
	logging.debug(("pollinterval   : %s") % (str(POLLINTERVAL)))
	logging.debug(("statustopic    : %s") % (str(STATUSTOPIC)))
	logging.debug(("sensors        : %s") % (len(sensors)))
	for owid, owtopic in sensors.items():
		logging.debug(("  %s : %s") % (owid, owtopic))
	logging.debug(("switches       : %s") % (len(switches)))
	for owid, owtopic in switches.items():
		logging.debug(("  %s : %s") % (owid, owtopic))
    
	# Connect to the broker and enter the main loop
	result = mqttc.connect(MQTT_HOST, MQTT_PORT, 60)
	while result != 0:
		logging.info("Connection failed with error code %s. Retrying", result)
		time.sleep(10)
		result = mqttc.connect(MQTT_HOST, MQTT_PORT, 60)

	# Define callbacks
	mqttc.on_connect = mqtt_on_connect
	mqttc.on_message = mqtt_on_message
	mqttc.on_disconnect = mqtt_on_disconnect
	mqttc.on_publish = mqtt_on_publish
	mqttc.on_log = mqtt_on_log

	# Connect to one wire server
	ow.init(("%s:%s") % (OW_HOST, str(OW_PORT))) 
	ow.error_level(ow.error_level.fatal)
	ow.error_print(ow.error_print.stderr)

	#test... try catch blabla...
#	for owid, owtopic in switches.items():
#		sens = ow.Sensor(owid)
#		sens.useCache(False)
#		#0 -> relai on
#		sens.PIO_A = '0'
#		time.sleep(0.2)
#		sens.PIO_A = '1'
#	while True:
#		for owid, owtopic in switches.items():
#			sw = ow.Sensor(owid)
#			sw.useCache(False)
#			#0 -> turned on
#			print(sw.sensed_B)
#			time.sleep(1)

	mqttc.loop_start()
	while True:
		# iterate over all switches
		for owid, owtopic in switches.items():
			logging.debug(("Querying %s : %s") % (owid, owtopic))
			try:
				switch = ow.Sensor(owid)
				owstate = 'True' if not switch.sensed_B else 'False'
				logging.debug(("Switch %s : %s") % (owid, owstate))
				mqttc.publish(owtopic, owstate)
			except ow.exUnknownSensor:
				logging.info("Switch exception for device %s - %s.", owid, owtopic)
			
			time.sleep(float(POLLINTERVAL) / (len(sensors) + len(switches)))

		# iterate over all sensors
                # simultaneous temperature conversion
                ow._put("/simultaneous/temperature","1")
		for owid, owtopic in sensors.items():
			logging.debug(("Querying %s : %s") % (owid, owtopic))
			try:             
				sensor = ow.Sensor(owid)                 
				owtemp = sensor.temperature            
				logging.debug(("Sensor %s : %s") % (owid, owtemp))
				mqttc.publish(owtopic, owtemp)
			except ow.exUnknownSensor:
				logging.info("Sensor exception for device %s - %s.", owid, owtopic)
        	    
			time.sleep(float(POLLINTERVAL) / (len(sensors) + len(switches)))


# Use the signal module to handle signals
signal.signal(signal.SIGTERM, cleanup)
signal.signal(signal.SIGINT, cleanup)

# start main loop
try:
    main_loop()
except KeyboardInterrupt:
    logging.info("Interrupted by keypress")
sys.exit(0)
