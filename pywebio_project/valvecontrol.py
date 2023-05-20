from pywebio import start_server
from pywebio.input import *
from pywebio.output import *
from pywebio.session import *
from pywebio.platform import *
import requests
import json
import time
from concurrent import futures
from datetime import datetime

url = "http://192.168.178.56/"
status="UNDEF"
closeExecutor = futures.ThreadPoolExecutor(max_workers=1)
openExecutor = futures.ThreadPoolExecutor(max_workers=1)
workInProgress = 0
maxworkInProgressTimeSeconds = 60
config(title="Frischluft control",theme="dark") 



def get_status():
    if(not lockDeviceOrError()):
        blockingProgressBarPageReload(5)
        return
    try:
        response = requests.post(url+"status")
        timestampPrint("Successful in POST /status")
    except:
        timestampPrint("Exception in POST /status")
        blockingProgressBarPageReload(5)
        return
    global status
    status = response.json()["status"]
    unlockDevice()

def get_status_text():
    if status != "OPEN" or status != "CLOSED":
    	return put_text(status).style('color:green') 
    return put_text("UNKNOWN").style('color:red; font-weight: bold')


def bmi():
    run_js('WebIO._state.CurrentSession.on_session_close(()=>{setTimeout(()=>location.reload(), 4000})')
    put_markdown("# Frischluft control")
    get_status()
    put_table([
        ['Status'],
        [get_status_text()]
    ])
    
    if(status != "OPEN"):
    	put_button("Open", onclick=openAsync)

    if(status != "CLOSED"):
        put_button("Close", onclick=closeAsync)

def lockDeviceOrError():
    global workInProgress
    if(workInProgress + maxworkInProgressTimeSeconds > time.time() ):
        put_error("Device is currently busy")
        timestampPrint("device is busy. workInProgress:"+str(workInProgress))
        return False
    workInProgress = time.time()
    return True

def unlockDevice():
    global workInProgress
    workInProgress = 0


def closeAsync():
    if(not lockDeviceOrError()):
        blockingProgressBarPageReload(5)
        return
    closeExecutor.submit(close)
    blockingProgressBar(35)
    unlockDevice()
    run_js('window.location.reload()')

def blockingProgressBar(seconds):
    put_progressbar('bar',auto_close=True)
    for i in range(seconds+1):
        set_progressbar('bar', i / seconds)
        time.sleep(1)

def blockingProgressBarPageReload(seconds):
    blockingProgressBar(seconds)
    run_js('window.location.reload()')

def openAsync():
    if(not lockDeviceOrError()):
        blockingProgressBarPageReload(5)
        return
    openExecutor.submit(open)
    blockingProgressBar(5)
    unlockDevice()
    run_js('window.location.reload()')


def close():
    try:
        response = requests.post(url+"close")
        timestampPrint("Successful in POST /close")
    except:
        timestampPrint("Exception in POST /close")
        blockingProgressBarPageReload(5)
        return


def timestampPrint(text):
    print(datetime.now().isoformat()+" "+text)


def open():
    try:
        response = requests.post(url+"open")
        timestampPrint("Successful in POST /open")
    except:
        timestampPrint("Exception in POST /open")
        blockingProgressBarPageReload(5)
        return

if __name__ == '__main__':

    start_server(bmi, port=8080, reconnect_timeout=10)
