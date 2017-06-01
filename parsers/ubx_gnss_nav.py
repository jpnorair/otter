
import sys
import json
import webbrowser

#i = ''
#line = ''
#while i != '\n':   # read one char at a time until NULL
#    i = sys.stdin.read(1)
#    line += i

line = raw_input()

jsonstr = line.decode("hex")

#Small hack to fix known bug in JSON input
jsonstr = jsonstr[::-1]
jsonstr = jsonstr.replace(',',"",1)
jsonstr = jsonstr[::-1]

parsed_json = json.loads(jsonstr)


#lat (float), lon (float), id (str)
lat = float(parsed_json['ubx_gnss_nav']['lat']) / 10000000.0
lon = float(parsed_json['ubx_gnss_nav']['lon']) / 10000000.0
id = parsed_json['ubx_gnss_nav']['id']

mapstr = "http://www.google.com/maps/place/?q=" + str(id) + "@" + str(lat) + "," + str(lon)

webbrowser.open(mapstr)

sys.stdout.write("id:  " + str(id) + '\n')
sys.stdout.write("lat: " + str(lat) + '\n')
sys.stdout.write("lon: " + str(lon) + '\n')
