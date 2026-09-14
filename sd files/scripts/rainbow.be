# rainbow.be — cycle the LED through colors (hold B to stop)
print("Rainbow - hold B to stop")
var hues = [[60,0,0],[60,40,0],[0,60,0],[0,40,40],[0,0,60],[40,0,40]]
for k : range(0, 40)
  var c = hues[k % 6]
  led(c[0], c[1], c[2])
  delay(120)
end
led(0, 0, 0)
