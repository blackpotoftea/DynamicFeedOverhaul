1. Detect if player is vampire lord or were-wolf - done
2. add animation trigger when in this forms - done but defau
3. refactor how animations are discover
    * repliate OAR conifig scan
    * check for file contain info femal/unisex/animation type, stage
4. get licnense https://www.shutterstock.com/image-vector/vampire-teeth-vector-isolated-on-black-1716039928
5. Refacto garph variable to be called DPA_
6. Check comon issue pointer and memory  - done
7. FormId cache for where it make sense - done

https://gitlab.com/jpstewart/commonlibsse-sample-plugin


test animation
fixed: km_front_feed_bite
passed: km_back_feed_bite km_front_feed_bloodrayne km_back_feed_jump_bite km_front_feed_bodyslam km_front_feed_decap

DEbug
werewolf:
player.addspell WerewolfChange
set PlayerIsWerewolf to 1


vampirelod
player.addspell DLC1VampireChange