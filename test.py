#!/usr/bin/python

import subprocess;
import os;

try :
	print subprocess.check_output('./fastzip -8 infozip.zip infozip'.split())
	print subprocess.check_output('./fastzip -0 obj.zip obj'.split())
	print subprocess.check_output('./fastzip -v --sign -Z infozip.zip igzip.zip igzip -0 -Z obj.zip fastzip=fs.stored -9 fastzip=packed/fastzip'.split())
	print subprocess.check_output('unzip -lv igzip.zip'.split())
	output = subprocess.check_output('unzip -t igzip.zip'.split())
	if output.find('No errors detected') >= 0 :
		print "Zip file OK"
	else :
		print output
	output = subprocess.check_output('jarsigner -verbose -certs -verify igzip.zip'.split())
	os.remove('igzip.zip')
	os.remove('obj.zip')
	os.remove('infozip.zip')
	if output.find('jar verified') >= 0 :
		print "Signing verfied OK"
	else :
		print output
except subprocess.CalledProcessError :
	print "Duh!"
