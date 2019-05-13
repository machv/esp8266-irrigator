function secondsToString(duration) {
    if(duration == 0) {
        return "00:00:00";
    }
    
    var sec_num = duration, hours, minutes, seconds;

    var hours   = Math.floor(sec_num / 3600);
    var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
    var seconds = sec_num - (hours * 3600) - (minutes * 60);

    hours = hours < 10 ? "0" + hours : hours;
    minutes = minutes < 10 ? "0" + minutes : minutes;
    seconds = seconds < 10 ? "0" + seconds : seconds;

    return hours + ':' + minutes + ':' + seconds;
}

function gebi(s) {
    return document.getElementById(s);
}

var countdowns = {}; // for countdowns
function processCountdowns() {    
    for (let key in countdowns) {
        let element = gebi('r' + key + "_timerCountdown");
        if(countdowns[key] > 0) {
            element.textContent = secondsToString(countdowns[key]);
        } else {
            element.textContent = "";
        }

        countdowns[key]--;
    }
}

function updateRelayCountdown(relay, secondsRemaining) {
    console.log("Updating timeout for relay #" + relay + " to " + secondsRemaining + " seconds.");
    countdowns[relay] = secondsRemaining;
}

function startCountdowns() {
    setInterval(processCountdowns, 1000);
}

function updateUi(jsonText) {
    var jsonResponse = JSON.parse(jsonText);

    if(jsonResponse.relays) {
        for(var i = 0; i < jsonResponse.relays.length; i++) {
            let relay = jsonResponse.relays[i];
                  
            if(relay.hasOwnProperty("timeout")) {
                updateRelayCountdown(i, relay.timeout);

                let timerElement = gebi("r" + i + "_timerCountdown");
                if(timerElement) {
                    if(relay.timeout <= 0) {
                        timerElement.innerHTML = "";
                    }
                }
            }

            let btn = gebi('r' + i + "_btn");
            btn.innerHTML = relay.state ? "Turn OFF" : "Turn ON";
            btn.className = relay.state ? "button button-on" : "button button-off";

            for (let key in relay) {
                if(key == "state") {
                    gebi("r" + i + "_state").innerHTML = relay.state ? "ON" : "OFF";
                    continue;
                }

                if (relay.hasOwnProperty(key)) {
                    console.log("[" + i + "] " + key + " = " + relay[key]);
                    let elem = gebi('r' + i + "_" + key);
                    if(elem)
                        elem.innerHTML = relay[key];
                }
            }    
        }
    }
}

var refresherInterval = 2000; // for UI changes
var x = null, lt;
function refresher(interval) {
    if(interval > 0) {
        refresherInterval = interval;
    }
    if (x != null) { 
        // Abort if no response
        x.abort();
    }    
    
    if (document.hasFocus()) {
        x = new XMLHttpRequest();
        x.onreadystatechange = function() {
            if(x.readyState == 4 && x.status == 200) {
                updateUi(x.responseText);
            }
        };
        x.open('GET', '/api/current', true);
        x.send();
    } else console.log("Skipping API update as document is not focused.");

    lt = setTimeout(refresher, refresherInterval); 
}
