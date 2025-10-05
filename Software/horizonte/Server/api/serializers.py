from rest_framework import serializers
from .models import AstroObject

class AstroObjectSerializer(serializers.ModelSerializer):
    class Meta:
        model = AstroObject
        fields = ['nombre', 'constelacion','info','imageUrl' ,'ra', 'dec']