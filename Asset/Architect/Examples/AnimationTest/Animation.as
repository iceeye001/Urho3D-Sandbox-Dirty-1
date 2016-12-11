class DirectionBlender
{
    void set_threshold(float threshold) { threshold_ = threshold; }
    void set_switchSpeed(float switchSpeed) { switchSpeed_ = switchSpeed; }
    void set_targetDirection(Vector2 targetDirection) { targetDirection_ = targetDirection; }
    Vector2 get_direction() { return direction_; }
    float get_time() { return time_; }

    void Update(float timeStep, float timeRate)
    {
        bool forceFast = direction_.length < epsilon_ || targetDirection_.length < epsilon_;
        bool fastSwitch = forceFast || direction_.Angle(targetDirection_) <= threshold_;
        if (fastSwitch)
            move(targetDirection_, timeStep, timeRate);
        else
        {
            float remaining = move(Vector2(0, 0), timeStep, timeRate);
            if (remaining > 0)
                move(targetDirection_, remaining, timeRate);
        }
    }
    
    private float move(Vector2 target, float timeStep, float timeRate)
    {
        Vector2 delta = target - direction_;
        float usedTime = Min(timeStep, delta.length / switchSpeed_);
        time_ += timeStep * timeRate;
        direction_ += delta.Normalized() * usedTime * switchSpeed_;
        if (direction_.length < epsilon_)
            time_ = 0;
        return timeStep - usedTime;
    }

    float epsilon_ = 0.0001;
    float threshold_ = 1;
    float switchSpeed_ = 1;
    
    Vector2 direction_ = Vector2(0, 0);
    Vector2 targetDirection_ = Vector2(0, 0);
    float time_ = 0;
};


class Animator : ScriptObject
{
    bool disableFoot = false;
    
    Vector3 _prevPosition;
    float _idleTimer;
    bool _idleCasted;
    WeightBlender _weightBlender;
    DirectionBlender _directionBlender;
    
    void DelayedStart()
    {
        AnimationController@ animController = node.GetComponent("AnimationController");
        AnimatedModel@ animModel = node.GetComponent("AnimatedModel");
        
        if (disableFoot)
        {
            animModel.skeleton.GetBone("swat:LeftUpLeg").animated = false;
            animModel.skeleton.GetBone("swat:LeftLeg").animated = false;
            animModel.skeleton.GetBone("swat:LeftFoot").animated = false;
            animModel.skeleton.GetBone("swat:RightUpLeg").animated = false;
            animModel.skeleton.GetBone("swat:RightLeg").animated = false;
            animModel.skeleton.GetBone("swat:RightFoot").animated = false;
        }
        
        /*animModel.skeleton.GetBone("swat:LeftShoulder").animated = false;
        animModel.skeleton.GetBone("swat:LeftArm").animated = false;
        animModel.skeleton.GetBone("swat:LeftForeArm").animated = false;
        animModel.skeleton.GetBone("swat:RightShoulder").animated = false;
        animModel.skeleton.GetBone("swat:RightArm").animated = false;
        animModel.skeleton.GetBone("swat:RightForeArm").animated = false;*/
        
        _prevPosition = node.position;
        _idleTimer = 0;
        _idleCasted = false;
        
        _directionBlender.threshold = 120;
        _directionBlender.switchSpeed = 3;
        _directionBlender.targetDirection = Vector2(0, 0);
    }

    void Update(float timeStep)
    {
        UpdateController(timeStep);
        ApplyMovement(timeStep);
    }
    
    private void updateAnimation(AnimationController@ animController, const String &in anim, float weight, float speed, float time, float epsilon)
    {
        if (weight > epsilon)
        {
            //if (animController.GetWeight(anim) < epsilon)
            {
                animController.Play(anim, 0, true);
                animController.SetTime(anim, time);
                //Print(anim + ":" + time);
            }
        }
        animController.SetSpeed(anim, speed);
        animController.SetWeight(anim, weight);
    }
    void UpdateController(float timeStep)
    {
        float idleTimeout = 0.5;
        String animIdle = "Swat_WalkZero.ani";
        String animFwd = "Swat_WalkFwd.ani";
        String animBwd = "Swat_WalkBwd.ani";
        String animLeft = "Swat_WalkLeft.ani";
        String animRight = "Swat_WalkRight.ani";
        
        AnimationController@ animController = node.GetComponent("AnimationController");
        Vector3 velocity = (node.position - _prevPosition) / timeStep;
        _prevPosition = node.position;
        
        Vector3 scaledVelocity = velocity / 1.7;
        if ((scaledVelocity * Vector3(1, 0, 1)).length < 0.01)
        {
            if (!_idleCasted && _idleTimer <= 0)
            {
                _idleCasted = true;
                _directionBlender.targetDirection = Vector2(0, 0);
            }
        }
        else
        {
            Vector2 direction = Vector2(scaledVelocity.x, -scaledVelocity.z) / (Abs(scaledVelocity.x) + Abs(scaledVelocity.z));
            _directionBlender.targetDirection = direction;
            
            _idleTimer = idleTimeout;
            _idleCasted = false;
        }
                
        if (_idleTimer > 0) _idleTimer -= timeStep;
        _directionBlender.Update(timeStep, scaledVelocity.length);

        Vector2 direction = _directionBlender.direction;
        float weightIdle = 1 - Abs(direction.x) - Abs(direction.y);
        float weightFwd = Max(0.0, direction.y);
        float weightBwd = Max(0.0, -direction.y);
        float weightLeft = Max(0.0, direction.x);
        float weightRight = Max(0.0, -direction.x);
        
        float epsilon = 0.0001;
        if (weightIdle > epsilon)
        {
            animController.Play(animIdle, 0, true);
            animController.SetWeight(animIdle, weightIdle);
            
            /*animController.Play("Swat_WalkFwd2.ani", 0, true);
            animController.SetWeight("Swat_WalkFwd2.ani", weightIdle / 4);
            animController.SetSpeed("Swat_WalkFwd2.ani", 0);
            
            animController.Play("Swat_WalkBwd2.ani", 0, true);
            animController.SetWeight("Swat_WalkBwd2.ani", weightIdle / 4);
            animController.SetSpeed("Swat_WalkBwd2.ani", 0);
            
            animController.Play("Swat_WalkLeft2.ani", 0, true);
            animController.SetWeight("Swat_WalkLeft2.ani", weightIdle / 4);
            animController.SetSpeed("Swat_WalkLeft2.ani", 0);
            
            animController.Play("Swat_WalkRight2.ani", 0, true);
            animController.SetWeight("Swat_WalkRight2.ani", weightIdle / 4);
            animController.SetSpeed("Swat_WalkRight2.ani", 0);*/
        }
        
        float time = _directionBlender.time;
        updateAnimation(animController, animFwd, weightFwd, scaledVelocity.length, Fract(time), epsilon);
        updateAnimation(animController, animBwd, weightBwd, scaledVelocity.length, Fract(time + 0.066666), epsilon);
        updateAnimation(animController, animLeft, weightLeft, scaledVelocity.length, Fract(time - 0.066666), epsilon);
        updateAnimation(animController, animRight, weightRight, scaledVelocity.length, Fract(time + 0.1), epsilon);
    }

    int movementType = 0;
    float movementScale = 5.0;
    float movementVelocity = 1.0;
    
    float _movementTime = 0;
    float _movementRemainingTime = 0;
    Vector3 _movementDirection;
    void ApplyMovement(float timeStep)
    {
        _movementTime += timeStep;
        switch (movementType)
        {
        case 1:
        {
            int dirIndex = int(_movementTime / movementScale) % 4;
            Vector3 dir = dirIndex == 0 ? Vector3(1, 0, 0) : dirIndex == 1 ? Vector3(0, 0, 1) : dirIndex == 2 ? Vector3(-1, 0, 0) : Vector3(0, 0, -1);
            node.position = node.position + dir * movementVelocity * timeStep;
            break;
        }
        case 2:
        {
            int dirIndex = int(_movementTime / movementScale) % 2;
            Vector3 dir = dirIndex == 0 ? Vector3(0, 0, 1) : Vector3(0, 0, -1);
            node.position = node.position + dir * movementVelocity * timeStep;
            break;
        }
        case 3:
        {
            int dirIndex = int(_movementTime / movementScale) % 2;
            Vector3 dir = dirIndex == 0 ? Vector3(1, 0, 3).Normalized() : Vector3(-1, 0, -3).Normalized();
            node.position = node.position + dir * movementVelocity * timeStep;
            break;
        }
        case 4:
        {
            int dirIndex = int(_movementTime / movementScale) % 2;
            Vector3 dir = dirIndex == 0 ? Vector3(-1, 0, 1) : Vector3(1, 0, -1);
            node.position = node.position + dir * movementVelocity * timeStep;
            break;
        }
        case 5:
        {
            if (_movementRemainingTime < 0)
            {
                _movementRemainingTime = Random(3.0, 4.0);
                _movementDirection = Vector3(Random(-1, 1), 0.0, Random(-1, 1)).Normalized();
            }
            node.position = node.position + _movementDirection * movementVelocity * timeStep;
            _movementRemainingTime -= timeStep;
            break;
        }
        }
    }
}
