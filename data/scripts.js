function secondsToString(duration) {
    var sec_num = duration, hours, minutes, seconds;

    var hours   = Math.floor(sec_num / 3600);
    var minutes = Math.floor((sec_num - (hours * 3600)) / 60);
    var seconds = sec_num - (hours * 3600) - (minutes * 60);

    hours = hours < 10 ? "0" + hours : hours;
    minutes = minutes < 10 ? "0" + minutes : minutes;
    seconds = seconds < 10 ? "0" + seconds : seconds;

    return hours + ':' + minutes + ':' + seconds;
}

function startTimer(duration, display) {
    var timer = duration, minutes, seconds;
    interval = setInterval(function () {
        display.textContent = secondsToString(duration);

        if (--timer < 0)
            display.textContent = "";
    }, 1000);

    return interval;
}

function gebi(s) {
    return document.getElementById(s);
}

var refresherInterval = 2000; // for UI changes
var intervals = {}; // for timer countdowns
var x = null, lt;

function updateUi(jsonText) {
    var jsonResponse = JSON.parse(jsonText);

    if(jsonResponse.relays) {
        //gebi("a").innerHTML = jsonResponse.relays.length;
        for(var i = 0; i < jsonResponse.relays.length; i++) {
            let relay = jsonResponse.relays[i];
            
            gebi("r" + i + "_state").innerHTML = relay.state ? "ON" : "OFF";
            
            clearInterval(intervals[i]);
            let timerElement = gebi("r" + i + "_timerCountdown");
            if(timerElement) {
                if(relay.timer > 0) {
                    timerElement.innerHTML = secondsToString(relay.timer);

                    intervals[i] = startTimer(relay.timer, timerElement);
                } else {
                    timerElement.innerHTML = "";
                }
            }

            let btn = gebi('r' + i + "_btn");
            btn.innerHTML = relay.state ? "Turn OFF" : "Turn ON";
            btn.className = relay.state ? "button button-on" : "button button-off";

            for (let key in relay) {
                if(key == "state" || key == "timer")
                    continue;

                if (relay.hasOwnProperty(key)) {
                    console.log(key + " = " + relay[key]);
                    let elem = gebi('r' + i + "_" + key);
                    if(elem)
                        elem.innerHTML = relay[key];
                }
            }    
        }
    }
}

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
        x.open('GET','/api/current', true);
        x.send();
    } else console.log("Skipping as not focused");

    lt = setTimeout(refresher, refresherInterval); 
}
