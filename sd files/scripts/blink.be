# blink.be — blink the onboard LED red 5 times
print("Blinking the LED 5 times")
for i : range(1, 5)
  led(60, 0, 0)
  delay(300)
  led(0, 0, 0)
  delay(300)
  print("tick " .. str(i))
end
led(0, 0, 0)
