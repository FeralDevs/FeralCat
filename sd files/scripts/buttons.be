# buttons.be — report presses for ~6 s (hold B to stop)
print("Press A / joystick up")
var t = millis()
while millis() - t < 6000
  if button("A")    print("A")   end
  if button("up")   print("up")  end
  delay(150)
end
print("done")
